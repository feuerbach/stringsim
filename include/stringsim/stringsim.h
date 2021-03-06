#ifndef STRINGSIM_H
#define STRINGSIM_H

#include <vector>
#include <algorithm>
#include "stringsim/matrix.h"

namespace stringsim {

struct Scoring {
  long match_value,
       mismatch_value,
       space_value,
       part_value;
  size_t min_len;
};

template<typename V> inline long compute_matrix_elt(Scoring scoring, const V &a, const V &b,
  const Matrix &matrix, size_t i, size_t j) {

  long upper = i == matrix.row_begin ? 0 : matrix(i-1, j),
       left  = j == matrix.col_begin ? 0 : matrix(i, j-1),
       upper_left = (i == matrix.row_begin || j == matrix.col_begin) ? 0 : matrix(i-1, j-1);

  long match_mistmatch_value = a[i] == b[j] ? scoring.match_value : scoring.mismatch_value;

  return std::max({
      0L,
      upper_left + match_mistmatch_value,
      left + scoring.space_value,
      upper + scoring.space_value
  });
}

template<typename V> void fill_matrix(Scoring scoring, const V &a, const V &b, Matrix &matrix) {
  if (matrix.row_begin >= matrix.row_end || matrix.col_begin >= matrix.col_end) {
    return;
  }

  for (size_t i = matrix.row_begin; i < matrix.row_end; i++) {
  for (size_t j = matrix.col_begin; j < matrix.col_end; j++) {
    matrix(i, j) = compute_matrix_elt(scoring, a, b, matrix, i, j);
  }}
}

template<typename V> Matrix find_alignment(Scoring scoring, const V &a, const V &b, const Matrix &matrix) {
  if (matrix.empty()) {
    return matrix;
  }
  size_t row_begin, row_end, col_begin, col_end;

  size_t i, j;
  std::tie(i,j) = matrix.max_element();

  row_begin = row_end = i + 1;
  col_begin = col_end = j + 1;

  long this_value;

  // traceback
  while ((this_value = matrix(i,j)) > 0) {
    row_begin = i;
    col_begin = j;

    if (i > matrix.row_begin && j > matrix.col_begin && this_value == matrix(i-1,j-1) + (a[i] == b[j] ? scoring.match_value : scoring.mismatch_value)) {
      i--; j--;
    } else if (i > matrix.row_begin && this_value == matrix(i-1,j) + scoring.space_value) {
      i--;
    } else if (j > matrix.col_begin && this_value == matrix(i,j-1) + scoring.space_value) {
      j--;
    } else {
      break; // e.g. we are at (0,3), and this is the beginning
    }
  }
  return Matrix(matrix, row_begin, row_end, col_begin, col_end);
}

// A helper function. Pushed a matrix into a vector unless the matrix is empty.
inline void push_if_not_empty(std::vector<Matrix> &vec, const Matrix &mx) {
  if (!mx.empty()) {
    vec.push_back(mx);
  }
}

// Given a vector of matrices and a local alignment, return:
//
// 1. The vector of matrices that don't intersect the alignment
// 2. The vector of matrices that intercepted the alignment, but split into
//    smaller matrices that do not.
inline std::pair<std::vector<Matrix>,std::vector<Matrix>> remove_alignment(
  std::vector<Matrix> matrices, Matrix alignment) {

  std::vector<Matrix> unaffected, affected;

  for (const Matrix &mx : matrices) {
    bool rows_intersect = !(mx.row_end <= alignment.row_begin || alignment.row_end <= mx.row_begin),
         cols_intersect = !(mx.col_end <= alignment.col_begin || alignment.col_end <= mx.col_begin);
    if (rows_intersect && cols_intersect) {
      // NB: the upper-left corner does not need updating!
      push_if_not_empty(unaffected, Matrix(mx, mx.row_begin, alignment.row_begin, mx.col_begin, alignment.col_begin));
      push_if_not_empty(affected, Matrix(mx, mx.row_begin, alignment.row_begin, alignment.col_end, mx.col_end));
      push_if_not_empty(affected, Matrix(mx, alignment.row_end, mx.row_end, mx.col_begin, alignment.col_begin));
      push_if_not_empty(affected, Matrix(mx, alignment.row_end, mx.row_end, alignment.col_end, mx.col_end));
    } else if (rows_intersect) {
      push_if_not_empty(affected, Matrix(mx, mx.row_begin, alignment.row_begin, mx.col_begin, mx.col_end));
      push_if_not_empty(affected, Matrix(mx, alignment.row_end, mx.row_end, mx.col_begin, mx.col_end));
    } else if (cols_intersect) {
      push_if_not_empty(affected, Matrix(mx, mx.row_begin, mx.row_end, mx.col_begin, alignment.col_begin));
      push_if_not_empty(affected, Matrix(mx, mx.row_begin, mx.row_end, alignment.col_end, mx.col_end));
    } else {
      unaffected.push_back(mx);
    }
  }
  return std::pair<std::vector<Matrix>,std::vector<Matrix>>(unaffected, affected);
}

// Update a recently cut off part of the matrix
template<typename V> void update_matrix(Scoring scoring, const V &a, const V &b, Matrix &matrix) {
  ssize_t last_updated_col_this = -1, last_updated_col_prev;

  for (size_t i = matrix.row_begin; i < matrix.row_end; i++) {

    last_updated_col_prev = last_updated_col_this;
    last_updated_col_this = -1;

    for (size_t j = matrix.col_begin; j < matrix.col_end; j++) {
      long newval = compute_matrix_elt(scoring, a, b, matrix, i, j);
      if (newval != matrix(i,j)) {
        matrix(i, j) = newval;
        last_updated_col_this = j;
      }
    }
  }
}

// Find the best alignment in a non-empty set of computed matrices
template<typename V> Matrix choose_alignment(Scoring scoring, const V &a, const V &b, const std::vector<Matrix> &matrices) {
  std::vector<long> scores(matrices.size());
  for (size_t i = 0; i < matrices.size(); i++) {
    scores[i] = matrices[i].max_value();
  }
  const Matrix &best_matrix = *(matrices.begin() + (std::max_element(scores.begin(), scores.end()) - scores.begin()));
  return find_alignment(scoring, a, b, best_matrix);
}

template<typename V> long similarity(Scoring scoring, const V &a, const V &b) {
  long total_score = 0L;
  Matrix matrix(a.size(), b.size());
  std::vector<Matrix> affected, unaffected;
  fill_matrix(scoring, a, b, matrix);
  std::vector<Matrix> matrices({matrix});
  while (!matrices.empty()) {
    Matrix alignment = choose_alignment(scoring, a, b, matrices);

    if (alignment.empty()
      || alignment.row_end - alignment.row_begin < scoring.min_len
      || alignment.col_end - alignment.col_begin < scoring.min_len) {
      break;
    }

    const long score_inc =
      // value of the alignment
      alignment(alignment.row_end-1, alignment.col_end-1)
      // penalty for adding another part
      + scoring.part_value;

    if (score_inc <= 0)
      break;

    total_score += score_inc;

    std::tie(unaffected, affected) = remove_alignment(matrices, alignment);

    // update the affected matrices
    for (Matrix &mx : affected) {
      update_matrix(scoring, a, b, mx);
    }
    // merge matrices together
    matrices = unaffected;
    matrices.insert(matrices.end(), affected.begin(), affected.end());
  };
  return total_score;
}

} // namespace stringsim

#endif // STRINGSIM_H
