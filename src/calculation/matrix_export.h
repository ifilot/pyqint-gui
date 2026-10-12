#pragma once

#include <QString>

#include "job_result.h"

enum class MatrixExportFormat { Excel, Csv, Text };

// Save the numeric matrix without headers, preserving double precision.
// Throws std::runtime_error on invalid data or a write failure.
void export_matrix(const DenseMatrix& matrix, const QString& path, MatrixExportFormat format);
