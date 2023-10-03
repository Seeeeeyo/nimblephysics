#include "AccelerationSmoother.hpp"

#include <iostream>

// #include <Eigen/Core>
// #include <Eigen/Dense>
#include <Eigen/IterativeLinearSolvers>
// #include <unsupported/Eigen/IterativeSolvers>

#include "dart/math/MathTypes.hpp"

namespace dart {
namespace utils {

/**
 * Create (and pre-factor) a smoother that can remove the "jerk" from a time
 * seriese of data.
 *
 * The alpha value will determine how much smoothing to apply. A value of 0
 * corresponds to no smoothing.
 */
AccelerationSmoother::AccelerationSmoother(
    int timesteps,
    s_t smoothingWeight,
    s_t regularizationWeight,
    bool useSparse,
    bool useIterativeSolver)
  : mTimesteps(timesteps),
    mSmoothingWeight(smoothingWeight),
    mRegularizationWeight(regularizationWeight),
    mUseSparse(useSparse),
    mUseIterativeSolver(useIterativeSolver),
    mIterations(10000)
{
  Eigen::Vector4s stamp;
  stamp << -1, 3, -3, 1;
  stamp *= mSmoothingWeight;
  mSmoothedTimesteps = max(0, mTimesteps - 3);

  if (useSparse)
  {
    typedef Eigen::Triplet<s_t> T;
    std::vector<T> tripletList;
    for (int i = 0; i < mSmoothedTimesteps; i++)
    {
      for (int j = 0; j < 4; j++)
      {
        tripletList.push_back(T(i, i + j, stamp(j)));
      }
    }
    for (int i = 0; i < mTimesteps; i++)
    {
      tripletList.push_back(T(mSmoothedTimesteps + i, i, 1));
    }
    mB_sparse
        = Eigen::SparseMatrix<s_t>(mSmoothedTimesteps + mTimesteps, mTimesteps);
    mB_sparse.setFromTriplets(tripletList.begin(), tripletList.end());
    mB_sparse.makeCompressed();
    if (!mUseIterativeSolver)
    {
      mB_sparseSolver.analyzePattern(mB_sparse);
      mB_sparseSolver.factorize(mB_sparse);
      if (mB_sparseSolver.info() != Eigen::Success)
      {
        std::cout << "mB_sparseSolver.factorize(mB_sparse) error: "
                  << mB_sparseSolver.lastErrorMessage() << std::endl;
      }
      assert(mB_sparseSolver.info() == Eigen::Success);
    }
  }
  else
  {
    mB = Eigen::MatrixXs::Zero(mSmoothedTimesteps + mTimesteps, mTimesteps);
    for (int i = 0; i < mSmoothedTimesteps; i++)
    {
      mB.block<1, 4>(i, i) = stamp;
    }
    mB.block(mSmoothedTimesteps, 0, mTimesteps, mTimesteps)
        = Eigen::MatrixXs::Identity(mTimesteps, mTimesteps);
    if (!mUseIterativeSolver)
    {
      mUseIterativeSolver = false;
      mFactoredB = Eigen::HouseholderQR<Eigen::MatrixXs>(mB);
    }
  }
};

/**
 * Adjust a time series of points to minimize the jerk (d/dt of acceleration)
 * implied by the position data. This will return a shorter time series, missing
 * the last 3 entries, because those cannot be smoothed by this technique.
 *
 * This method assumes that the `series` matrix has `mTimesteps` number of
 * columns, and each column represents a complete joint configuration at that
 * timestep.
 */
Eigen::MatrixXs AccelerationSmoother::smooth(Eigen::MatrixXs series)
{
  assert(series.cols() == mTimesteps);

  Eigen::MatrixXs smoothed = Eigen::MatrixXs::Zero(series.rows(), mTimesteps);

  for (int row = 0; row < series.rows(); row++)
  {
    // If all the values in this row are identical, it's probably a locked
    // joint, and we can't smooth it.
    if (series.row(row).maxCoeff() == series.row(row).minCoeff())
    {
      smoothed.row(row) = series.row(row);
      continue;
    }
    Eigen::VectorXs c = Eigen::VectorXs::Zero(mSmoothedTimesteps + mTimesteps);
    c.segment(mSmoothedTimesteps, mTimesteps)
        = mRegularizationWeight * series.row(row);
    if (mUseIterativeSolver)
    {
      if (mUseSparse)
      {
        int iterations = mIterations;
        for (int i = 0; i < 6; i++) {
          Eigen::LeastSquaresConjugateGradient<Eigen::SparseMatrix<s_t>> solver;
          solver.compute(mB_sparse);
          solver.setTolerance(1e-10);
          solver.setMaxIterations(iterations);
          smoothed.row(row) = solver.solveWithGuess(c, series.row(row))
                              * (1.0 / mRegularizationWeight);
          // Check convergence
          if (solver.info() == Eigen::Success) {
            // Converged
            break;
          } else {
            std::cout << "LeastSquaresConjugateGradient did not converge in " << iterations << ", with error " << solver.error() << " so doubling iteration count and trying again." << std::endl;
            iterations *= 2;
          }
        }
      }
      else
      {
        int iterations = mIterations;
        for (int i = 0; i < 6; i++) {
          Eigen::LeastSquaresConjugateGradient<Eigen::MatrixXs> cg;
          cg.compute(mB);
          cg.setTolerance(1e-10);
          cg.setMaxIterations(iterations);
          smoothed.row(row) = cg.solveWithGuess(c, series.row(row))
                              * (1.0 / mRegularizationWeight);
          // Check convergence
          if (cg.info() == Eigen::Success) {
            // Converged
            break;
          } else {
            std::cout << "LeastSquaresConjugateGradient did not converge in " << iterations << ", with error " << cg.error() << " so doubling iteration count and trying again." << std::endl;
            iterations *= 2;
          }
        }
      }
    }
    else
    {
      // Eigen::VectorXs deltas = mB.completeOrthogonalDecomposition().solve(c);
      if (mUseSparse)
      {
        smoothed.row(row)
            = mB_sparseSolver.solve(c) * (1.0 / mRegularizationWeight);
        assert(mB_sparseSolver.info() == Eigen::Success);
      }
      else
      {
        smoothed.row(row) = mFactoredB.solve(c) * (1.0 / mRegularizationWeight);
      }
    }
  }

  return smoothed;
};

/**
 * If we're using an iterative solver, this sets the number of iterations that
 * the iterative solver will use to find the least squares minimum-jerk
 * solution. For particularly stiff problems (where the ratio between
 * smoothingWeight and regularizationWeight is greater than 1e6 or so) we'll
 * want to increase this number to something like 100,000.
 */
void AccelerationSmoother::setIterations(int iterations)
{
  mIterations = iterations;
}

/**
 * This computes the squared loss for this smoother, given a time series and a
 * set of perturbations `delta` to the time series.
 */
s_t AccelerationSmoother::getLoss(
    Eigen::MatrixXs series, Eigen::MatrixXs originalSeries, bool debug)
{
  s_t manual_score = 0.0;
  for (int row = 0; row < series.rows(); row++)
  {
    for (int i = 0; i < mSmoothedTimesteps; i++)
    {
      /*
      s_t vt = series(i + 1) - series(i);
      s_t vt_1 = series(i + 2) - series(i + 1);
      s_t vt_2 = series(i + 3) - series(i + 2);
      */
      s_t vt = series(row, i + 1) - series(row, i);
      s_t vt_1 = series(row, i + 2) - series(row, i + 1);
      s_t vt_2 = series(row, i + 3) - series(row, i + 2);
      s_t at = vt_1 - vt;
      s_t at_1 = vt_2 - vt_1;
      s_t jt = at_1 - at;
      s_t jtScaled = mSmoothingWeight * jt;

      if (debug)
      {
        std::cout << "Jerk " << i << ": " << jt << std::endl;
        std::cout << "Manual: " << jtScaled * jtScaled << std::endl;
      }

      manual_score += jtScaled * jtScaled;
    }

    for (int i = 0; i < mTimesteps; i++)
    {
      s_t diff = series(row, i) - originalSeries(row, i);
      diff *= mRegularizationWeight;
      manual_score += diff * diff;
    }

    if (debug)
    {
      std::cout << "Manual score: " << manual_score << std::endl;
    }
  }

  return manual_score;
}

/**
 * This prints the stats for a time-series of data, with pos, vel, accel, and
 * jerk
 */
void AccelerationSmoother::debugTimeSeries(Eigen::VectorXs series)
{
  Eigen::MatrixXs cols = Eigen::MatrixXs::Zero(series.size() - 3, 4);
  for (int i = 0; i < series.size() - 3; i++)
  {
    s_t pt = series(i);
    s_t vt = series(i + 1) - series(i);
    s_t vt_1 = series(i + 2) - series(i + 1);
    s_t vt_2 = series(i + 3) - series(i + 2);
    s_t at = vt_1 - vt;
    s_t at_1 = vt_2 - vt_1;
    s_t jt = at - at_1;
    cols(i, 0) = pt;
    cols(i, 1) = vt;
    cols(i, 2) = at;
    cols(i, 3) = jt;
  }

  std::cout << "pos - vel - acc - jerk" << std::endl << cols << std::endl;
}

} // namespace utils
} // namespace dart