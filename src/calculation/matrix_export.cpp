#include "matrix_export.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include <QDataStream>
#include <QSaveFile>
#include <QXmlStreamWriter>

namespace {

QString number(double value) {
    return QString::number(value, 'g', std::numeric_limits<double>::max_digits10);
}

QByteArray delimited_matrix(const DenseMatrix& matrix, char separator) {
    QByteArray data;
    for(int row = 0; row < matrix.rows; ++row) {
        for(int col = 0; col < matrix.cols; ++col) {
            if(col > 0) {
                data += separator;
            }
            data += number(matrix(row, col)).toUtf8();
        }
        data += '\n';
    }
    return data;
}

QString column_name(int column) {
    QString name;
    for(++column; column > 0; column = (column - 1) / 26) {
        name.prepend(QChar('A' + (column - 1) % 26));
    }
    return name;
}

quint32 crc32(const QByteArray& data) {
    quint32 crc = 0xffffffffu;
    for(unsigned char byte : data) {
        crc ^= byte;
        for(int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
        }
    }
    return crc ^ 0xffffffffu;
}

// A small ZIP writer using uncompressed entries. XLSX needs only five XML
// parts here; no external spreadsheet library or Python runtime is required.
QByteArray zip_parts(const std::vector<std::pair<QByteArray, QByteArray>>& parts) {
    QByteArray archive;
    QDataStream stream(&archive, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    std::vector<quint32> offsets;
    std::vector<quint32> checksums;
    for(const auto& part : parts) {
        if(quint64(archive.size()) + quint64(part.second.size()) + part.first.size() + 30 >=
           std::numeric_limits<quint32>::max()) {
            throw std::runtime_error("Excel export exceeds the ZIP size limit.");
        }
        offsets.push_back(quint32(archive.size()));
        checksums.push_back(crc32(part.second));
        stream << quint32(0x04034b50) << quint16(20) << quint16(0) << quint16(0)
               << quint16(0) << quint16(0x21) << checksums.back()
               << quint32(part.second.size()) << quint32(part.second.size())
               << quint16(part.first.size()) << quint16(0);
        stream.writeRawData(part.first.constData(), part.first.size());
        stream.writeRawData(part.second.constData(), part.second.size());
    }
    const quint32 directory_offset = quint32(archive.size());
    for(size_t i = 0; i < parts.size(); ++i) {
        const auto& part = parts[i];
        stream << quint32(0x02014b50) << quint16(20) << quint16(20)
               << quint16(0) << quint16(0) << quint16(0) << quint16(0x21)
               << checksums[i] << quint32(part.second.size()) << quint32(part.second.size())
               << quint16(part.first.size()) << quint16(0) << quint16(0)
               << quint16(0) << quint16(0) << quint32(0) << offsets[i];
        stream.writeRawData(part.first.constData(), part.first.size());
    }
    const quint32 directory_size = quint32(archive.size()) - directory_offset;
    stream << quint32(0x06054b50) << quint16(0) << quint16(0)
           << quint16(parts.size()) << quint16(parts.size())
           << directory_size << directory_offset << quint16(0);
    if(stream.status() != QDataStream::Ok) {
        throw std::runtime_error("Could not create the Excel workbook.");
    }
    return archive;
}

QByteArray excel_matrix(const DenseMatrix& matrix) {
    if(matrix.rows > 1048576 || matrix.cols > 16384) {
        throw std::runtime_error("Matrix exceeds Excel's worksheet dimensions.");
    }
    QByteArray worksheet;
    QXmlStreamWriter xml(&worksheet);
    xml.writeStartDocument();
    xml.writeStartElement("worksheet");
    xml.writeDefaultNamespace("http://schemas.openxmlformats.org/spreadsheetml/2006/main");
    xml.writeStartElement("sheetData");
    for(int row = 0; row < matrix.rows; ++row) {
        xml.writeStartElement("row");
        xml.writeAttribute("r", QString::number(row + 1));
        for(int col = 0; col < matrix.cols; ++col) {
            xml.writeStartElement("c");
            xml.writeAttribute("r", column_name(col) + QString::number(row + 1));
            xml.writeTextElement("v", number(matrix(row, col)));
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();

    return zip_parts({
        {"[Content_Types].xml", R"(<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
</Types>)"},
        {"_rels/.rels", R"(<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>)"},
        {"xl/workbook.xml", R"(<?xml version="1.0" encoding="UTF-8"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<sheets><sheet name="Matrix" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
        {"xl/_rels/workbook.xml.rels", R"(<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
</Relationships>)"},
        {"xl/worksheets/sheet1.xml", worksheet}
    });
}

} // namespace

void export_matrix(const DenseMatrix& matrix, const QString& path, MatrixExportFormat format) {
    if(matrix.rows <= 0 || matrix.cols <= 0 ||
       matrix.data.size() != size_t(matrix.rows) * size_t(matrix.cols)) {
        throw std::runtime_error("Cannot export an empty or invalid matrix.");
    }
    for(double value : matrix.data) {
        if(!std::isfinite(value)) {
            throw std::runtime_error("Cannot export a matrix containing non-finite values.");
        }
    }
    QByteArray data;
    switch(format) {
        case MatrixExportFormat::Excel: data = excel_matrix(matrix); break;
        case MatrixExportFormat::Csv: data = delimited_matrix(matrix, ','); break;
        case MatrixExportFormat::Text: data = delimited_matrix(matrix, '\t'); break;
    }
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        throw std::runtime_error(file.errorString().toStdString());
    }
}
