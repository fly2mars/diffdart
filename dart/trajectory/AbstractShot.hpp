#ifndef DART_TRAJECTORY_ABSTRACT_SHOT_HPP_
#define DART_TRAJECTORY_ABSTRACT_SHOT_HPP_

#include <memory>

#include <Eigen/Dense>

#include "dart/trajectory/TrajectoryConstants.hpp"

namespace dart {
namespace simulation {
class World;
}

namespace trajectory {

class AbstractShot
{
public:
  friend class IPOptShotWrapper;

  /// Default constructor
  AbstractShot(std::shared_ptr<simulation::World> world);

  /// Abstract destructor
  virtual ~AbstractShot();

  /// Returns the length of the flattened problem state
  virtual int getFlatProblemDim() const = 0;

  /// Returns the length of the knot-point constraint vector
  virtual int getConstraintDim() const = 0;

  void setLossFunction(TrajectoryLossFn loss);

  void setLossFunctionGradient(TrajectoryLossFnGrad lossGrad);

  /// This copies a shot down into a single flat vector
  virtual void flatten(/* OUT */ Eigen::Ref<Eigen::VectorXd> flat) const = 0;

  /// This gets the parameters out of a flat vector
  virtual void unflatten(const Eigen::Ref<const Eigen::VectorXd>& flat) = 0;

  /// This runs the shot out, and writes the positions, velocities, and forces
  virtual void unroll(
      std::shared_ptr<simulation::World> world,
      /* OUT */ Eigen::Ref<Eigen::MatrixXd> poses,
      /* OUT */ Eigen::Ref<Eigen::MatrixXd> vels,
      /* OUT */ Eigen::Ref<Eigen::MatrixXd> forces)
      = 0;

  /// This gets the fixed upper bounds for a flat vector, used during
  /// optimization
  virtual void getUpperBounds(
      std::shared_ptr<simulation::World> world,
      /* OUT */ Eigen::Ref<Eigen::VectorXd> flat) const = 0;

  /// This gets the fixed lower bounds for a flat vector, used during
  /// optimization
  virtual void getLowerBounds(
      std::shared_ptr<simulation::World> world,
      /* OUT */ Eigen::Ref<Eigen::VectorXd> flat) const = 0;

  /// This returns the initial guess for the values of X when running an
  /// optimization
  virtual void getInitialGuess(
      std::shared_ptr<simulation::World> world,
      /* OUT */ Eigen::Ref<Eigen::VectorXd> flat) const = 0;

  /// This computes the values of the constraints
  virtual void computeConstraints(
      std::shared_ptr<simulation::World> world,
      /* OUT */ Eigen::Ref<Eigen::VectorXd> constraints)
      = 0;

  /// This computes the Jacobian that relates the flat problem to the end state.
  /// This returns a matrix that's (2 * mNumDofs, getFlatProblemDim()).
  virtual void backpropJacobian(
      std::shared_ptr<simulation::World> world,
      /* OUT */ Eigen::Ref<Eigen::MatrixXd> jac)
      = 0;

  /// This computes the gradient in the flat problem space, taking into accounts
  /// incoming gradients with respect to any of the shot's values.
  virtual void backpropGradient(
      std::shared_ptr<simulation::World> world,
      const Eigen::Ref<const Eigen::MatrixXd>& gradWrtPoses,
      const Eigen::Ref<const Eigen::MatrixXd>& gradWrtVels,
      const Eigen::Ref<const Eigen::MatrixXd>& gradWrtForces,
      /* OUT */ Eigen::Ref<Eigen::VectorXd> grad)
      = 0;

  /// This computes finite difference gradients of (poses, vels, forces)
  /// matrices with respect to a passed in loss function. If there aren't
  /// analytical gradients of the loss, then this is a useful pre-step for
  /// analytically computing the gradients for backprop.
  void bruteForceGradOfLossInputs(
      std::shared_ptr<simulation::World> world,
      TrajectoryLossFn loss,
      /* OUT */ Eigen::Ref<Eigen::MatrixXd> gradWrtPoses,
      /* OUT */ Eigen::Ref<Eigen::MatrixXd> gradWrtVels,
      /* OUT */ Eigen::Ref<Eigen::MatrixXd> gradWrtForces);

  /// This populates the passed in matrices with the values from this trajectory
  virtual void getStates(
      std::shared_ptr<simulation::World> world,
      /* OUT */ Eigen::Ref<Eigen::MatrixXd> poses,
      /* OUT */ Eigen::Ref<Eigen::MatrixXd> vels,
      /* OUT */ Eigen::Ref<Eigen::MatrixXd> forces,
      bool useKnots = true)
      = 0;

  /// This returns the concatenation of (start pos, start vel) for convenience
  virtual Eigen::VectorXd getStartState() = 0;

  /// This unrolls the shot, and returns the (pos, vel) state concatenated at
  /// the end of the shot
  virtual Eigen::VectorXd getFinalState(
      std::shared_ptr<simulation::World> world)
      = 0;

  int getNumSteps();

  /// This returns the debugging name of a given DOF
  virtual std::string getFlatDimName(int dim) = 0;

  /// This gets the number of non-zero entries in the Jacobian
  virtual int getNumberNonZeroJacobian() = 0;

  /// This gets the structure of the non-zero entries in the Jacobian
  virtual void getJacobianSparsityStructure(
      Eigen::Ref<Eigen::VectorXi> rows, Eigen::Ref<Eigen::VectorXi> cols)
      = 0;

  /// This writes the Jacobian to a sparse vector
  virtual void getSparseJacobian(
      std::shared_ptr<simulation::World> world,
      Eigen::Ref<Eigen::VectorXd> sparse)
      = 0;

  //////////////////////////////////////////////////////////////////////////////
  // For Testing
  //////////////////////////////////////////////////////////////////////////////

  /// This computes finite difference Jacobians analagous to backpropJacobians()
  void finiteDifferenceJacobian(
      std::shared_ptr<simulation::World> world,
      Eigen::Ref<Eigen::MatrixXd> jac);

  /// This computes finite difference Jacobians analagous to
  /// backpropGradient()
  void finiteDifferenceGradient(
      std::shared_ptr<simulation::World> world,
      TrajectoryLossFn loss,
      /* OUT */ Eigen::Ref<Eigen::VectorXd> grad);

  /// This computes the Jacobians that relate each timestep to the endpoint of
  /// the trajectory. For a timestep at time t, this will relate quantities like
  /// v_t -> p_end, for example.
  TimestepJacobians backpropStartStateJacobians(
      std::shared_ptr<simulation::World> world);

  /// This computes finite difference Jacobians analagous to
  /// backpropStartStateJacobians()
  TimestepJacobians finiteDifferenceStartStateJacobians(
      std::shared_ptr<simulation::World> world, double EPS);

protected:
  int mSteps;
  int mNumDofs;
  bool mTuneStartingState;
  std::shared_ptr<simulation::World> mWorld;
  TrajectoryLossFn mLoss;
  TrajectoryLossFnGrad mGrad;
  Eigen::MatrixXd mPoses;
  Eigen::MatrixXd mVels;
  Eigen::MatrixXd mForces;
};

} // namespace trajectory
} // namespace dart

#endif