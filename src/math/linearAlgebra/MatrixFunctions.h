/**
 * @file MatrixFunctions.h
 *
 * @date Apr 17, 2019
 * @author: Moritz Bensberg
 * @copyright \n
 *  This file is part of the program Serenity.\n\n
 *  Serenity is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, either version 3 of
 *  the License, or (at your option) any later version.\n\n
 *  Serenity is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.\n\n
 *  You should have received a copy of the GNU Lesser General
 *  Public License along with Serenity.
 *  If not, see <http://www.gnu.org/licenses/>.\n
 */

#ifndef MATH_LINEARALGEBRA_MATRIXFUNCTIONS_H_
#define MATH_LINEARALGEBRA_MATRIXFUNCTIONS_H_

/* Include Serenity Internal Headers */
#include "data/matrices/MatrixInBasis.h"
#include "misc/SerenityError.h" //Error messages
/* Include Std and External Headers */
#include <Eigen/Dense> //Dense matrices and eigenvalue decompositions.
#include <cmath>       //squre root.

namespace Serenity {
/**
 * @brief Calculate an arbitrary function of a symmetric matrix.
 * @param matrix The matrix.
 * @param f The function.
 * @return f(matrix).
 */
inline Eigen::MatrixXd mFunc_Sym(const Eigen::MatrixXd& matrix, std::function<double(const double&)> f) {
  assert(matrix.cols() == matrix.rows());
  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eigensolver(matrix);
  Eigen::VectorXd eigenvals = eigensolver.eigenvalues().eval();
  const Eigen::MatrixXd eigenvectors = eigensolver.eigenvectors().eval();
  for (unsigned int col = 0; col < matrix.cols(); ++col) {
    eigenvals(col) = f(eigenvals(col));
  }
  return (eigenvectors * eigenvals.asDiagonal() * eigenvectors.transpose()).eval();
}

/**
 * @brief Calculates the square root of the given matrix.
 * @param matrix The matrix.
 * @return sqrt(matrix).
 */
inline Eigen::MatrixXd mSqrt_Sym(const Eigen::MatrixXd& matrix) {
  auto const func = [&](const double& e) {
    if (e < 0.0)
      throw SerenityError("You are trying to calculate the square root of a negative number!");
    return sqrt(e);
  }; // func
  return mFunc_Sym(matrix, func);
}

/**
 * @brief Calculates the square root of the pseudo inverse of the given matrix.
 * @param matrix The matrix.
 * @param threshold Threshold for setting entries in the inverse to zero.
 * @return matrix^(-1/2).
 */
inline Eigen::MatrixXd pseudoInversSqrt_Sym(const Eigen::MatrixXd& matrix, double threshold = 1e-6) {
  auto const func = [&](const double& e) {
    if (e < -1.0)
      throw SerenityError("Tolerance of negative eigenvalues in the pseudo inverse exceeded! You are trying to "
                          "calculate the square root of a negative number!");
    return (e >= threshold) ? 1.0 / sqrt(e) : 0.0;
  }; // func
  return mFunc_Sym(matrix, func);
}

/**
 * @brief Calculates the pseudo inverse of the given matrix.
 * @param matrix The matrix.
 * @param threshold Threshold for setting entries in the inverse to zero.
 * @return matrix^(-1).
 */
inline Eigen::MatrixXd pseudoInvers_Sym(const Eigen::MatrixXd& matrix, double threshold = 1e-6) {
  auto const func = [&](const double& e) { return (std::abs(e) >= threshold) ? 1.0 / e : 0.0; }; // func
  return mFunc_Sym(matrix, func);
}
/**
 * @brief symmetrize the given matrix.
 * @param matrix The matrix to be symmetrized.
 * @return the symmetrized matrix.
 */
inline Eigen::MatrixXd symmetrize(const Eigen::MatrixXd& matrix) {
  return 0.5 * (matrix + matrix.transpose()).eval();
}
/**
 * @brief symmetrize the given matrix.
 * @param matrix The matrix to be symmetrized.
 * @return the symmetrized matrix.
 */
template<Options::SCF_MODES SCFMode>
inline MatrixInBasis<SCFMode> symmetrize(const MatrixInBasis<SCFMode>& matrix) {
  MatrixInBasis<SCFMode> sym(matrix.getBasisController());
  for_spin(sym, matrix) {
    sym_spin = symmetrize(Eigen::MatrixXd(matrix_spin));
  };
  return sym;
}
/**
 * @brief symmetrize the given matrix in place.
 * @param matrix The matrix to be symmetrized.
 */
template<Options::SCF_MODES SCFMode>
void symInPlace(MatrixInBasis<SCFMode>& matrix) {
  for_spin(matrix) {
    matrix_spin = 0.5 * ((Eigen::MatrixXd)matrix_spin + (Eigen::MatrixXd)matrix_spin.transpose()).eval();
  };
}
/**
 * @brief Orthogonalize the columns of the given matrix with respect to the given metric
 *        using cholesky decomposition. Note that this implies that mat.transpose() * metric * mat
 *        is positive definite.
 * @param mat The matrix to be orthogonalized.
 * @param metric The metric.
 * @return The orthogonalized matrix.
 */
inline Eigen::MatrixXd orthogonalize_chol(const Eigen::MatrixXd& mat, const Eigen::MatrixXd metric) {
  Eigen::LLT<Eigen::MatrixXd> llt = (mat.transpose() * metric * mat).llt();
  const Eigen::MatrixXd l = llt.matrixL();
  return (mat * (l.inverse()).transpose()).eval();
}

} /* namespace Serenity */
#endif /* MATH_LINEARALGEBRA_MATRIXFUNCTIONS_H_ */
