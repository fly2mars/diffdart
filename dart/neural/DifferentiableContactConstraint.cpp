#include "dart/neural/DifferentiableContactConstraint.hpp"

#include "dart/collision/Contact.hpp"
#include "dart/constraint/ConstraintBase.hpp"
#include "dart/constraint/ContactConstraint.hpp"
#include "dart/dynamics/DegreeOfFreedom.hpp"
#include "dart/dynamics/Joint.hpp"
#include "dart/dynamics/Skeleton.hpp"
#include "dart/neural/BackpropSnapshot.hpp"
#include "dart/neural/RestorableSnapshot.hpp"
#include "dart/simulation/World.hpp"

namespace dart {
namespace neural {

//==============================================================================
DifferentiableContactConstraint::DifferentiableContactConstraint(
    std::shared_ptr<constraint::ConstraintBase> constraint, int index)
{
  mConstraint = constraint;
  mIndex = index;
  if (mConstraint->isContactConstraint())
  {
    mContactConstraint
        = std::static_pointer_cast<constraint::ContactConstraint>(mConstraint);
    // This needs to be explicitly copied, otherwise the memory is overwritten
    mContact = std::make_shared<collision::Contact>(
        mContactConstraint->getContact());
  }
  for (auto skel : constraint->getSkeletons())
  {
    mSkeletons.push_back(skel->getName());
  }
}

//==============================================================================
Eigen::Vector3d DifferentiableContactConstraint::getContactWorldPosition()
{
  if (!mConstraint->isContactConstraint())
  {
    return Eigen::Vector3d::Zero();
  }
  return mContact->point;
}

//==============================================================================
Eigen::Vector3d DifferentiableContactConstraint::getContactWorldNormal()
{
  if (!mConstraint->isContactConstraint())
  {
    return Eigen::Vector3d::Zero();
  }
  return mContact->normal;
}

//==============================================================================
Eigen::Vector3d DifferentiableContactConstraint::getContactWorldForceDirection()
{
  if (!mConstraint->isContactConstraint())
  {
    return Eigen::Vector3d::Zero();
  }
  if (mIndex == 0)
  {
    return mContact->normal;
  }
  else
  {
    return mContactConstraint->getTangentBasisMatrixODE(mContact->normal)
        .col(mIndex - 1);
  }
}

//==============================================================================
Eigen::Vector6d DifferentiableContactConstraint::getWorldForce()
{
  Eigen::Vector6d worldForce = Eigen::Vector6d();
  worldForce.head<3>()
      = getContactWorldPosition().cross(getContactWorldForceDirection());
  worldForce.tail<3>() = getContactWorldForceDirection();
  return worldForce;
}

//==============================================================================
collision::ContactType DifferentiableContactConstraint::getContactType()
{
  if (!mConstraint->isContactConstraint())
  {
    // UNSUPPORTED is the default, and means we won't attempt to get gradients
    // for how the contact point moves as we move the skeletons.
    return collision::ContactType::UNSUPPORTED;
  }
  return mContact->type;
}

//==============================================================================
/// This figures out what type of contact this skeleton is involved in.
DofContactType DifferentiableContactConstraint::getDofContactType(
    dynamics::DegreeOfFreedom* dof)
{
  bool isParentA = isParent(dof, mContactConstraint->getBodyNodeA());
  bool isParentB = isParent(dof, mContactConstraint->getBodyNodeB());
  // If we're a parent of both contact points, it's a self-contact down the tree
  if (isParentA && isParentB)
  {
    switch (getContactType())
    {
      case collision::ContactType::FACE_VERTEX:
      case collision::ContactType::VERTEX_FACE:
        return DofContactType::VERTEX_FACE_SELF_COLLISION;
      case collision::ContactType::EDGE_EDGE:
        return DofContactType::EDGE_EDGE_SELF_COLLISION;
      default:
        return DofContactType::UNSUPPORTED;
    }
  }
  // If we're not a parent of either point, it's not an issue
  else if (!isParentA && !isParentB)
  {
    return DofContactType::NONE;
  }
  // If we're just a parent of A
  else if (isParentA)
  {
    switch (getContactType())
    {
      case collision::ContactType::FACE_VERTEX:
        return DofContactType::FACE;
      case collision::ContactType::VERTEX_FACE:
        return DofContactType::VERTEX;
      case collision::ContactType::EDGE_EDGE:
        return DofContactType::EDGE_B;
      default:
        return DofContactType::UNSUPPORTED;
    }
  }
  // If we're just a parent of B
  else if (isParentB)
  {
    switch (getContactType())
    {
      case collision::ContactType::FACE_VERTEX:
        return DofContactType::VERTEX;
      case collision::ContactType::VERTEX_FACE:
        return DofContactType::FACE;
      case collision::ContactType::EDGE_EDGE:
        return DofContactType::EDGE_A;
      default:
        return DofContactType::UNSUPPORTED;
    }
  }

  // Control should never reach this point
  return DofContactType::NONE;
}

//==============================================================================
Eigen::VectorXd DifferentiableContactConstraint::getConstraintForces(
    std::shared_ptr<dynamics::Skeleton> skel)
{
  // If this constraint doesn't touch this skeleton, then return all 0s
  if (std::find(mSkeletons.begin(), mSkeletons.end(), skel->getName())
      == mSkeletons.end())
  {
    return Eigen::VectorXd::Zero(skel->getNumDofs());
  }

  Eigen::Vector6d worldForce = getWorldForce();

  Eigen::VectorXd taus = Eigen::VectorXd(skel->getNumDofs());
  for (int i = 0; i < skel->getNumDofs(); i++)
  {
    auto dof = skel->getDof(i);
    double multiple = getForceMultiple(dof);
    if (multiple == 0)
    {
      taus(i) = 0.0;
    }
    else
    {
      Eigen::Vector6d worldTwist = getWorldScrewAxis(dof);
      taus(i) = worldTwist.dot(worldForce) * multiple;
    }
  }

  return taus;
}

//==============================================================================
Eigen::VectorXd DifferentiableContactConstraint::getConstraintForces(
    std::shared_ptr<simulation::World> world)
{
  Eigen::VectorXd taus = Eigen::VectorXd(world->getNumDofs());
  int cursor = 0;
  for (int i = 0; i < world->getNumSkeletons(); i++)
  {
    auto skel = world->getSkeleton(i);
    int dofs = skel->getNumDofs();
    taus.segment(cursor, dofs) = getConstraintForces(skel);
    cursor += dofs;
  }
  return taus;
}

//==============================================================================
/// Returns the gradient of the contact position with respect to the
/// specified dof of this skeleton
Eigen::Vector3d DifferentiableContactConstraint::getContactPositionGradient(
    dynamics::DegreeOfFreedom* dof)
{
  Eigen::Vector3d contactPos = getContactWorldPosition();
  DofContactType type = getDofContactType(dof);

  if (type == FACE)
  {
    return Eigen::Vector3d::Zero();
  }
  else if (
      type == VERTEX || type == VERTEX_FACE_SELF_COLLISION
      || type == EDGE_EDGE_SELF_COLLISION || type == EDGE_A || type == EDGE_B)
  {
    int jointIndex = dof->getIndexInJoint();
    dynamics::BodyNode* childNode = dof->getChildBodyNode();
    // TODO:opt getRelativeJacobian() creates a whole matrix, when we only want
    // a single column.
    Eigen::Vector6d worldTwist = math::AdT(
        childNode->getWorldTransform(),
        dof->getJoint()->getRelativeJacobian().col(jointIndex));
    if (type == VERTEX || type == VERTEX_FACE_SELF_COLLISION
        || type == EDGE_EDGE_SELF_COLLISION)
    {
      return math::gradientWrtTheta(worldTwist, contactPos, 0.0);
    }
    else if (type == EDGE_A)
    {
      Eigen::Vector3d edgeAPosGradient
          = math::gradientWrtTheta(worldTwist, mContact->edgeAFixedPoint, 0.0);
      Eigen::Vector3d edgeADirGradient = math::gradientWrtThetaPureRotation(
          worldTwist.head<3>(), mContact->edgeADir, 0.0);

      return math::getContactPointGradient(
          mContact->edgeAFixedPoint,
          edgeAPosGradient,
          mContact->edgeADir,
          edgeADirGradient,
          mContact->edgeBFixedPoint,
          Eigen::Vector3d::Zero(),
          mContact->edgeBDir,
          Eigen::Vector3d::Zero());
    }
    else if (type == EDGE_B)
    {
      Eigen::Vector3d edgeBPosGradient
          = math::gradientWrtTheta(worldTwist, mContact->edgeBFixedPoint, 0.0);
      Eigen::Vector3d edgeBDirGradient = math::gradientWrtThetaPureRotation(
          worldTwist.head<3>(), mContact->edgeBDir, 0.0);

      return math::getContactPointGradient(
          mContact->edgeAFixedPoint,
          Eigen::Vector3d::Zero(),
          mContact->edgeADir,
          Eigen::Vector3d::Zero(),
          mContact->edgeBFixedPoint,
          edgeBPosGradient,
          mContact->edgeBDir,
          edgeBDirGradient);
    }
  }

  else
  {
    // Default case
    return Eigen::Vector3d::Zero();
  }
}

//==============================================================================
/// Returns the gradient of the contact normal with respect to the
/// specified dof of this skeleton
Eigen::Vector3d DifferentiableContactConstraint::getContactNormalGradient(
    dynamics::DegreeOfFreedom* dof)
{
  Eigen::Vector3d normal = getContactWorldNormal();
  DofContactType type = getDofContactType(dof);
  if (type == VERTEX)
  {
    return Eigen::Vector3d::Zero();
  }
  else if (
      type == FACE || type == VERTEX_FACE_SELF_COLLISION || type == EDGE_A
      || type == EDGE_B || type == EDGE_EDGE_SELF_COLLISION)
  {
    int jointIndex = dof->getIndexInJoint();
    dynamics::BodyNode* childNode = dof->getChildBodyNode();
    // TODO:opt getRelativeJacobian() creates a whole matrix, when we only want
    // a single column.
    Eigen::Vector6d worldTwist = math::AdT(
        childNode->getWorldTransform(),
        dof->getJoint()->getRelativeJacobian().col(jointIndex));

    if (type == FACE || type == VERTEX_FACE_SELF_COLLISION
        || type == EDGE_EDGE_SELF_COLLISION)
    {
      return math::gradientWrtThetaPureRotation(
          worldTwist.head<3>(), normal, 0);
    }
    else if (type == EDGE_A)
    {
      Eigen::Vector3d edgeADirGradient = math::gradientWrtThetaPureRotation(
          worldTwist.head<3>(), mContact->edgeADir, 0.0);

      return edgeADirGradient.cross(mContact->edgeBDir);
    }
    else if (type == EDGE_B)
    {
      Eigen::Vector3d edgeBDirGradient = math::gradientWrtThetaPureRotation(
          worldTwist.head<3>(), mContact->edgeBDir, 0.0);

      return mContact->edgeADir.cross(edgeBDirGradient);
    }
  }
  else
  {
    // Default case
    return Eigen::Vector3d::Zero();
  }
}

//==============================================================================
/// Returns the gradient of the contact force with respect to the
/// specified dof of this skeleton
Eigen::Vector3d DifferentiableContactConstraint::getContactForceGradient(
    dynamics::DegreeOfFreedom* dof)
{
  DofContactType type = getDofContactType(dof);
  if (type == VERTEX)
  {
    return Eigen::Vector3d::Zero();
  }
  else if (
      type == FACE || type == VERTEX_FACE_SELF_COLLISION || type == EDGE_A
      || type == EDGE_B || type == EDGE_EDGE_SELF_COLLISION)
  {
    Eigen::Vector3d contactNormal = getContactWorldNormal();
    Eigen::Vector3d normalGradient = getContactNormalGradient(dof);
    if (mIndex == 0 || normalGradient.squaredNorm() <= 1e-12)
      return normalGradient;
    else
    {
      return mContactConstraint
          ->getTangentBasisMatrixODEGradient(contactNormal, normalGradient)
          .col(mIndex - 1);
    }
  }
  else
  {
    // Default case
    return Eigen::Vector3d::Zero();
  }
}

//==============================================================================
/// Returns the gradient of the full 6d twist force
Eigen::Vector6d DifferentiableContactConstraint::getContactWorldForceGradient(
    dynamics::DegreeOfFreedom* dof)
{
  Eigen::Vector3d position = getContactWorldPosition();
  Eigen::Vector3d force = getContactWorldForceDirection();
  Eigen::Vector3d forceGradient = getContactForceGradient(dof);
  Eigen::Vector3d positionGradient = getContactPositionGradient(dof);

  Eigen::Vector6d result = Eigen::Vector6d::Zero();
  result.head<3>()
      = position.cross(forceGradient) + positionGradient.cross(force);
  result.tail<3>() = forceGradient;
  return result;
}

//==============================================================================
EdgeData DifferentiableContactConstraint::getEdgeGradient(
    dynamics::DegreeOfFreedom* dof)
{
  EdgeData data;
  data.edgeAPos = Eigen::Vector3d::Zero();
  data.edgeADir = Eigen::Vector3d::Zero();
  data.edgeBPos = Eigen::Vector3d::Zero();
  data.edgeBDir = Eigen::Vector3d::Zero();

  int jointIndex = dof->getIndexInJoint();
  dynamics::BodyNode* childNode = dof->getChildBodyNode();
  // TODO:opt getRelativeJacobian() creates a whole matrix, when we only want
  // a single column.
  Eigen::Vector6d worldTwist = math::AdT(
      childNode->getWorldTransform(),
      dof->getJoint()->getRelativeJacobian().col(jointIndex));

  DofContactType type = getDofContactType(dof);
  if (type == EDGE_A)
  {
    data.edgeAPos
        = math::gradientWrtTheta(worldTwist, mContact->edgeAFixedPoint, 0.0);
    data.edgeADir = math::gradientWrtThetaPureRotation(
        worldTwist.head<3>(), mContact->edgeADir, 0.0);
  }
  else if (type == EDGE_B)
  {
    data.edgeBPos
        = math::gradientWrtTheta(worldTwist, mContact->edgeBFixedPoint, 0.0);
    data.edgeBDir = math::gradientWrtThetaPureRotation(
        worldTwist.head<3>(), mContact->edgeBDir, 0.0);
  }
  else if (type == EDGE_EDGE_SELF_COLLISION)
  {
    data.edgeAPos
        = math::gradientWrtTheta(worldTwist, mContact->edgeAFixedPoint, 0.0);
    data.edgeADir = math::gradientWrtThetaPureRotation(
        worldTwist.head<3>(), mContact->edgeADir, 0.0);
    data.edgeBPos
        = math::gradientWrtTheta(worldTwist, mContact->edgeBFixedPoint, 0.0);
    data.edgeBDir = math::gradientWrtThetaPureRotation(
        worldTwist.head<3>(), mContact->edgeBDir, 0.0);
  }

  return data;
}

//==============================================================================
/// Returns the gradient of the screw axis with respect to the rotate dof
Eigen::Vector6d DifferentiableContactConstraint::getScrewAxisGradient(
    dynamics::DegreeOfFreedom* screwDof, dynamics::DegreeOfFreedom* rotateDof)
{
  if (!isParent(rotateDof, screwDof))
    return Eigen::Vector6d::Zero();

  Eigen::Vector6d axisWorldTwist = getWorldScrewAxis(screwDof);
  Eigen::Vector6d rotateWorldTwist = getWorldScrewAxis(rotateDof);
  return math::ad(rotateWorldTwist, axisWorldTwist);
}

//==============================================================================
/// This is the analytical Jacobian for the contact position
math::LinearJacobian
DifferentiableContactConstraint::getContactPositionJacobian(
    std::shared_ptr<simulation::World> world)
{
  math::LinearJacobian jac = math::LinearJacobian::Zero(3, world->getNumDofs());
  int i = 0;
  for (auto dof : world->getDofs())
  {
    jac.col(i) = getContactPositionGradient(dof);
    i++;
  }
  return jac;
}

//==============================================================================
/// This is the analytical Jacobian for the contact position
math::LinearJacobian
DifferentiableContactConstraint::getContactPositionJacobian(
    std::shared_ptr<dynamics::Skeleton> skel)
{
  math::LinearJacobian jac = math::LinearJacobian::Zero(3, skel->getNumDofs());
  int i = 0;
  for (auto dof : skel->getDofs())
  {
    jac.col(i) = getContactPositionGradient(dof);
    i++;
  }
  return jac;
}

//==============================================================================
/// This is the analytical Jacobian for the contact normal
math::LinearJacobian
DifferentiableContactConstraint::getContactForceDirectionJacobian(
    std::shared_ptr<simulation::World> world)
{
  math::LinearJacobian jac = math::LinearJacobian::Zero(3, world->getNumDofs());
  int i = 0;
  for (auto dof : world->getDofs())
  {
    jac.col(i) = getContactForceGradient(dof);
    i++;
  }
  return jac;
}

//==============================================================================
/// This is the analytical Jacobian for the contact normal
math::LinearJacobian
DifferentiableContactConstraint::getContactForceDirectionJacobian(
    std::shared_ptr<dynamics::Skeleton> skel)
{
  math::LinearJacobian jac = math::LinearJacobian::Zero(3, skel->getNumDofs());
  int i = 0;
  for (auto dof : skel->getDofs())
  {
    jac.col(i) = getContactForceGradient(dof);
    i++;
  }
  return jac;
}

//==============================================================================
math::Jacobian DifferentiableContactConstraint::getContactForceJacobian(
    std::shared_ptr<simulation::World> world)
{
  Eigen::Vector3d pos = getContactWorldPosition();
  Eigen::Vector3d dir = getContactWorldForceDirection();
  math::LinearJacobian posJac = getContactPositionJacobian(world);
  math::LinearJacobian dirJac = getContactForceDirectionJacobian(world);
  math::Jacobian jac = math::Jacobian::Zero(6, world->getNumDofs());

  // tau = pos cross dir
  for (int i = 0; i < world->getNumDofs(); i++)
  {
    jac.block<3, 1>(0, i) = pos.cross(dirJac.col(i)) + posJac.col(i).cross(dir);
  }
  // f = dir
  jac.block(3, 0, 3, world->getNumDofs()) = dirJac;

  return jac;
}

//==============================================================================
math::Jacobian DifferentiableContactConstraint::getContactForceJacobian(
    std::shared_ptr<dynamics::Skeleton> skel)
{
  Eigen::Vector3d pos = getContactWorldPosition();
  Eigen::Vector3d dir = getContactWorldForceDirection();
  math::LinearJacobian posJac = getContactPositionJacobian(skel);
  math::LinearJacobian dirJac = getContactForceDirectionJacobian(skel);
  math::Jacobian jac = math::Jacobian::Zero(6, skel->getNumDofs());

  // tau = pos cross dir
  for (int i = 0; i < skel->getNumDofs(); i++)
  {
    jac.block<3, 1>(0, i) = pos.cross(dirJac.col(i)) + posJac.col(i).cross(dir);
  }
  // f = dir
  jac.block(3, 0, 3, skel->getNumDofs()) = dirJac;

  return jac;
}

//==============================================================================
/// This gets the constraint force for a given DOF
double DifferentiableContactConstraint::getConstraintForce(
    dynamics::DegreeOfFreedom* dof)
{
  double multiple = getForceMultiple(dof);
  Eigen::Vector6d worldForce = getWorldForce();
  Eigen::Vector6d worldTwist = getWorldScrewAxis(dof);
  return worldTwist.dot(worldForce) * multiple;
}

//==============================================================================
/// This gets the gradient of constraint force at this joint with respect to
/// another joint
double DifferentiableContactConstraint::getConstraintForceDerivative(
    dynamics::DegreeOfFreedom* dof, dynamics::DegreeOfFreedom* wrt)
{
  double multiple = getForceMultiple(dof);
  Eigen::Vector6d worldForce = getWorldForce();
  Eigen::Vector6d gradientOfWorldForce = getContactWorldForceGradient(wrt);
  Eigen::Vector6d gradientOfWorldTwist = getScrewAxisGradient(dof, wrt);
  Eigen::Vector6d worldTwist = getWorldScrewAxis(dof);
  double dot1 = worldTwist.dot(gradientOfWorldForce);
  double dot2 = gradientOfWorldTwist.dot(worldForce);
  double sum = dot1 + dot2;
  double ret = sum * multiple;
  return (worldTwist.dot(gradientOfWorldForce)
          + gradientOfWorldTwist.dot(worldForce))
         * multiple;
}

//==============================================================================
/// This returns an analytical Jacobian relating the skeletons that this
/// contact touches.
Eigen::MatrixXd DifferentiableContactConstraint::getConstraintForcesJacobian(
    std::shared_ptr<simulation::World> world)
{
  int dim = world->getNumDofs();
  math::Jacobian forceJac = getContactForceJacobian(world);
  Eigen::Vector6d force = getWorldForce();

  Eigen::MatrixXd result = Eigen::MatrixXd::Zero(dim, dim);
  std::vector<dynamics::DegreeOfFreedom*> dofs = world->getDofs();
  for (int row = 0; row < dim; row++)
  {
    Eigen::Vector6d axis = getWorldScrewAxis(dofs[row]);
    for (int wrt = 0; wrt < dim; wrt++)
    {
      Eigen::Vector6d screwAxisGradient
          = getScrewAxisGradient(dofs[row], dofs[wrt]);
      Eigen::Vector6d forceGradient = forceJac.col(wrt);
      double multiple = getForceMultiple(dofs[row]);
      result(row, wrt)
          = multiple * (screwAxisGradient.dot(force) + axis.dot(forceGradient));
    }
  }

  return result;
}

//==============================================================================
/// This computes and returns the analytical Jacobian relating how changes in
/// the positions of wrt's DOFs changes the constraint forces on skel.
Eigen::MatrixXd DifferentiableContactConstraint::getConstraintForcesJacobian(
    std::shared_ptr<dynamics::Skeleton> skel,
    std::shared_ptr<dynamics::Skeleton> wrt)
{
  math::Jacobian forceJac = getContactForceJacobian(wrt);
  Eigen::Vector6d force = getWorldForce();

  Eigen::MatrixXd result
      = Eigen::MatrixXd::Zero(skel->getNumDofs(), wrt->getNumDofs());
  for (int row = 0; row < skel->getNumDofs(); row++)
  {
    Eigen::Vector6d axis = getWorldScrewAxis(skel->getDof(row));
    for (int col = 0; col < wrt->getNumDofs(); col++)
    {
      Eigen::Vector6d screwAxisGradient
          = getScrewAxisGradient(skel->getDof(row), wrt->getDof(col));
      Eigen::Vector6d forceGradient = forceJac.col(col);
      double multiple = getForceMultiple(skel->getDof(row));
      result(row, col)
          = multiple * (screwAxisGradient.dot(force) + axis.dot(forceGradient));
    }
  }

  return result;
}

//==============================================================================
/// This computes and returns the analytical Jacobian relating how changes in
/// the positions of wrt's DOFs changes the constraint forces on all the
/// skels.
Eigen::MatrixXd DifferentiableContactConstraint::getConstraintForcesJacobian(
    std::vector<std::shared_ptr<dynamics::Skeleton>> skels,
    std::shared_ptr<dynamics::Skeleton> wrt)
{
  math::Jacobian forceJac = getContactForceJacobian(wrt);
  Eigen::Vector6d force = getWorldForce();

  int numRows = 0;
  for (auto skel : skels)
    numRows += skel->getNumDofs();

  Eigen::MatrixXd result = Eigen::MatrixXd::Zero(numRows, wrt->getNumDofs());

  int row = 0;
  for (auto skel : skels)
  {
    for (int i = 0; i < skel->getNumDofs(); i++)
    {
      Eigen::Vector6d axis = getWorldScrewAxis(skel->getDof(i));
      for (int col = 0; col < wrt->getNumDofs(); col++)
      {
        Eigen::Vector6d screwAxisGradient
            = getScrewAxisGradient(skel->getDof(i), wrt->getDof(col));
        Eigen::Vector6d forceGradient = forceJac.col(col);
        double multiple = getForceMultiple(skel->getDof(i));
        result(row, col)
            = multiple
              * (screwAxisGradient.dot(force) + axis.dot(forceGradient));
      }
      row++;
    }
  }

  return result;
}

//==============================================================================
/// This computes and returns the analytical Jacobian relating how changes in
/// the positions of any of the DOFs changes the constraint forces on all the
/// skels.
Eigen::MatrixXd DifferentiableContactConstraint::getConstraintForcesJacobian(
    std::vector<std::shared_ptr<dynamics::Skeleton>> skels)
{
  int dofs = 0;
  for (auto skel : skels)
    dofs += skel->getNumDofs();

  Eigen::MatrixXd result = Eigen::MatrixXd::Zero(dofs, dofs);

  int cursor = 0;
  for (auto skel : skels)
  {
    result.block(0, cursor, dofs, skel->getNumDofs())
        = getConstraintForcesJacobian(skels, skel);
    cursor += skel->getNumDofs();
  }

  return result;
}

//==============================================================================
/// The linear Jacobian for the contact position
math::LinearJacobian
DifferentiableContactConstraint::bruteForceContactPositionJacobian(
    std::shared_ptr<simulation::World> world)
{
  RestorableSnapshot snapshot(world);

  int dofs = world->getNumDofs();
  math::LinearJacobian jac = math::LinearJacobian(3, dofs);

  const double EPS = 1e-6;

  Eigen::VectorXd positions = world->getPositions();

  for (int i = 0; i < dofs; i++)
  {
    snapshot.restore();
    Eigen::VectorXd perturbedPositions = positions;
    perturbedPositions(i) += EPS;
    world->setPositions(perturbedPositions);

    std::shared_ptr<BackpropSnapshot> backpropSnapshot
        = neural::forwardPass(world, true);
    std::shared_ptr<DifferentiableContactConstraint> peerConstraint
        = getPeerConstraint(backpropSnapshot);
    jac.col(i) = (peerConstraint->getContactWorldPosition()
                  - getContactWorldPosition())
                 / EPS;
  }

  snapshot.restore();

  return jac;
}

//==============================================================================
/// The linear Jacobian for the contact normal
math::LinearJacobian
DifferentiableContactConstraint::bruteForceContactForceDirectionJacobian(
    std::shared_ptr<simulation::World> world)
{
  RestorableSnapshot snapshot(world);

  int dofs = world->getNumDofs();
  math::LinearJacobian jac = math::LinearJacobian(3, dofs);

  const double EPS = 1e-6;

  Eigen::VectorXd positions = world->getPositions();

  for (int i = 0; i < dofs; i++)
  {
    snapshot.restore();
    Eigen::VectorXd perturbedPositions = positions;
    perturbedPositions(i) += EPS;
    world->setPositions(perturbedPositions);

    std::shared_ptr<BackpropSnapshot> backpropSnapshot
        = neural::forwardPass(world, true);
    std::shared_ptr<DifferentiableContactConstraint> peerConstraint
        = getPeerConstraint(backpropSnapshot);
    jac.col(i) = (peerConstraint->getContactWorldForceDirection()
                  - getContactWorldForceDirection())
                 / EPS;
  }

  snapshot.restore();

  return jac;
}

//==============================================================================
/// This is the brute force version of getWorldForceJacobian()
math::Jacobian DifferentiableContactConstraint::bruteForceContactForceJacobian(
    std::shared_ptr<simulation::World> world)
{
  RestorableSnapshot snapshot(world);

  int dofs = world->getNumDofs();
  math::Jacobian jac = math::Jacobian(6, dofs);

  const double EPS = 1e-6;

  Eigen::VectorXd positions = world->getPositions();

  for (int i = 0; i < dofs; i++)
  {
    snapshot.restore();
    Eigen::VectorXd perturbedPositions = positions;
    perturbedPositions(i) += EPS;
    world->setPositions(perturbedPositions);

    std::shared_ptr<BackpropSnapshot> backpropSnapshot
        = neural::forwardPass(world, true);
    std::shared_ptr<DifferentiableContactConstraint> peerConstraint
        = getPeerConstraint(backpropSnapshot);
    jac.col(i) = (peerConstraint->getWorldForce() - getWorldForce()) / EPS;
  }

  snapshot.restore();

  return jac;
}

//==============================================================================
/// This is the brute force version of getConstraintForcesJacobian()
Eigen::MatrixXd
DifferentiableContactConstraint::bruteForceConstraintForcesJacobian(
    std::shared_ptr<simulation::World> world)
{
  int dims = world->getNumDofs();
  Eigen::MatrixXd result = Eigen::MatrixXd::Zero(dims, dims);

  RestorableSnapshot snapshot(world);

  Eigen::VectorXd originalPosition = world->getPositions();
  const double EPS = 1e-7;

  std::shared_ptr<BackpropSnapshot> originalBackpropSnapshot
      = neural::forwardPass(world, true);
  std::shared_ptr<DifferentiableContactConstraint> originalPeerConstraint
      = getPeerConstraint(originalBackpropSnapshot);
  Eigen::VectorXd originalOut
      = originalPeerConstraint->getConstraintForces(world);

  for (int i = 0; i < dims; i++)
  {
    Eigen::VectorXd tweakedPosition = originalPosition;
    tweakedPosition(i) += EPS;
    world->setPositions(tweakedPosition);

    std::shared_ptr<BackpropSnapshot> backpropSnapshot
        = neural::forwardPass(world, true);
    std::shared_ptr<DifferentiableContactConstraint> peerConstraint
        = getPeerConstraint(backpropSnapshot);
    Eigen::VectorXd newOut = peerConstraint->getConstraintForces(world);

    result.col(i) = (newOut - originalOut) / EPS;
  }

  snapshot.restore();

  return result;
}

//==============================================================================
Eigen::Vector3d
DifferentiableContactConstraint::estimatePerturbedContactPosition(
    std::shared_ptr<dynamics::Skeleton> skel, int dofIndex, double eps)
{
  Eigen::Vector3d contactPos = getContactWorldPosition();
  DofContactType type = getDofContactType(skel->getDof(dofIndex));

  if (type == VERTEX || type == VERTEX_FACE_SELF_COLLISION
      || type == EDGE_EDGE_SELF_COLLISION)
  {
    Eigen::Vector6d worldTwist = getWorldScrewAxis(skel, dofIndex);
    Eigen::Isometry3d rotation = math::expMap(worldTwist * eps);
    Eigen::Vector3d perturbedContactPos = rotation * contactPos;
    return perturbedContactPos;
  }
  else if (type == FACE)
  {
    return contactPos;
  }
  else if (type == EDGE_A)
  {
    Eigen::Vector6d worldTwist = getWorldScrewAxis(skel, dofIndex);
    Eigen::Isometry3d translation = math::expMap(worldTwist * eps);
    Eigen::Isometry3d rotation = translation;
    rotation.translation().setZero();
    Eigen::Vector3d contactPoint = math::getContactPoint(
        translation * mContact->edgeAFixedPoint,
        rotation * mContact->edgeADir,
        mContact->edgeBFixedPoint,
        mContact->edgeBDir);
    return contactPoint;
  }
  else if (type == EDGE_B)
  {
    Eigen::Vector6d worldTwist = getWorldScrewAxis(skel, dofIndex);
    Eigen::Isometry3d translation = math::expMap(worldTwist * eps);
    Eigen::Isometry3d rotation = translation;
    rotation.translation().setZero();
    Eigen::Vector3d contactPoint = math::getContactPoint(
        mContact->edgeAFixedPoint,
        mContact->edgeADir,
        translation * mContact->edgeBFixedPoint,
        rotation * mContact->edgeBDir);
    return contactPoint;
  }
  else
  {
    // Default case
    return contactPos;
  }
}

//==============================================================================
Eigen::Vector3d DifferentiableContactConstraint::estimatePerturbedContactNormal(
    std::shared_ptr<dynamics::Skeleton> skel, int dofIndex, double eps)
{
  Eigen::Vector3d normal = getContactWorldNormal();
  DofContactType type = getDofContactType(skel->getDof(dofIndex));
  if (type == VERTEX)
  {
    return normal;
  }
  else if (
      type == FACE || type == VERTEX_FACE_SELF_COLLISION || type == EDGE_A
      || type == EDGE_B || type == EDGE_EDGE_SELF_COLLISION)
  {
    Eigen::Vector6d worldTwist = getWorldScrewAxis(skel, dofIndex);
    Eigen::Isometry3d rotation = math::expMap(worldTwist * eps);
    rotation.translation().setZero();
    if (type == FACE || type == VERTEX_FACE_SELF_COLLISION
        || type == EDGE_EDGE_SELF_COLLISION)
    {
      Eigen::Vector3d perturbedNormal = rotation * normal;
      return perturbedNormal;
    }
    else if (type == EDGE_A)
    {
      Eigen::Vector3d normal
          = (rotation * mContact->edgeADir).cross(mContact->edgeBDir);
      return normal;
    }
    else if (type == EDGE_B)
    {
      Eigen::Vector3d normal
          = mContact->edgeADir.cross(rotation * mContact->edgeBDir);
      return normal;
    }
  }
  else
  {
    // Default case
    return normal;
  }
}

//==============================================================================
Eigen::Vector3d
DifferentiableContactConstraint::estimatePerturbedContactForceDirection(
    std::shared_ptr<dynamics::Skeleton> skel, int dofIndex, double eps)
{
  Eigen::Vector3d forceDir = getContactWorldForceDirection();
  DofContactType type = getDofContactType(skel->getDof(dofIndex));
  if (type == VERTEX)
  {
    return forceDir;
  }
  else if (
      type == FACE || type == VERTEX_FACE_SELF_COLLISION || type == EDGE_A
      || type == EDGE_B || type == EDGE_EDGE_SELF_COLLISION)
  {
    Eigen::Vector3d contactNormal
        = estimatePerturbedContactNormal(skel, dofIndex, eps);
    if (mIndex == 0)
      return contactNormal;
    else
    {
      return mContactConstraint->getTangentBasisMatrixODE(contactNormal)
          .col(mIndex - 1);
    }
  }
  else
  {
    // Default case
    return forceDir;
  }
}

//==============================================================================
/// Just for testing: This analytically estimates how edges will move under a
/// perturbation
EdgeData DifferentiableContactConstraint::estimatePerturbedEdges(
    std::shared_ptr<dynamics::Skeleton> skel, int dofIndex, double eps)
{
  EdgeData data;
  data.edgeAPos = Eigen::Vector3d::Zero();
  data.edgeADir = Eigen::Vector3d::Zero();
  data.edgeBPos = Eigen::Vector3d::Zero();
  data.edgeBDir = Eigen::Vector3d::Zero();

  dynamics::DegreeOfFreedom* dof = skel->getDof(dofIndex);
  DofContactType type = getDofContactType(dof);

  Eigen::Vector6d worldTwist = getWorldScrewAxis(skel, dofIndex);
  Eigen::Isometry3d translation = math::expMap(worldTwist * eps);
  Eigen::Isometry3d rotation = translation;
  rotation.translation().setZero();

  if (type == EDGE_A)
  {
    data.edgeAPos = translation * mContact->edgeAFixedPoint;
    data.edgeADir = rotation * mContact->edgeADir;
    data.edgeBPos = mContact->edgeBFixedPoint;
    data.edgeBDir = mContact->edgeBDir;
  }
  else if (type == EDGE_B)
  {
    data.edgeAPos = mContact->edgeAFixedPoint;
    data.edgeADir = mContact->edgeADir;
    data.edgeBPos = translation * mContact->edgeBFixedPoint;
    data.edgeBDir = rotation * mContact->edgeBDir;
  }
  else if (type == EDGE_EDGE_SELF_COLLISION)
  {
    data.edgeAPos = translation * mContact->edgeAFixedPoint;
    data.edgeADir = rotation * mContact->edgeADir;
    data.edgeBPos = translation * mContact->edgeBFixedPoint;
    data.edgeBDir = rotation * mContact->edgeBDir;
  }

  return data;
}

//==============================================================================
EdgeData DifferentiableContactConstraint::getEdges()
{
  EdgeData data;
  data.edgeAPos = Eigen::Vector3d::Zero();
  data.edgeADir = Eigen::Vector3d::Zero();
  data.edgeBPos = Eigen::Vector3d::Zero();
  data.edgeBDir = Eigen::Vector3d::Zero();

  if (getContactType() == collision::ContactType::EDGE_EDGE)
  {
    data.edgeAPos = mContact->edgeAFixedPoint;
    data.edgeADir = mContact->edgeADir;
    data.edgeBPos = mContact->edgeBFixedPoint;
    data.edgeBDir = mContact->edgeBDir;
  }

  return data;
}

//==============================================================================
/// Just for testing: This analytically estimates how a screw axis will move
/// when rotated by another screw.
Eigen::Vector6d DifferentiableContactConstraint::estimatePerturbedScrewAxis(
    dynamics::DegreeOfFreedom* axis,
    dynamics::DegreeOfFreedom* rotate,
    double eps)
{
  Eigen::Vector6d axisWorldTwist = getWorldScrewAxis(axis);
  if (!isParent(rotate, axis))
    return axisWorldTwist;
  Eigen::Vector6d rotateWorldTwist = getWorldScrewAxis(rotate);
  Eigen::Isometry3d transform = math::expMap(rotateWorldTwist * eps);
  Eigen::Vector6d transformedAxis = math::AdT(transform, axisWorldTwist);
  return transformedAxis;
}

//==============================================================================
void DifferentiableContactConstraint::setOffsetIntoWorld(
    int offset, bool isUpperBoundConstraint)
{
  mOffsetIntoWorld = offset;
  mIsUpperBoundConstraint = isUpperBoundConstraint;
}

//==============================================================================
Eigen::Vector3d
DifferentiableContactConstraint::bruteForcePerturbedContactPosition(
    std::shared_ptr<simulation::World> world,
    std::shared_ptr<dynamics::Skeleton> skel,
    int dofIndex,
    double eps)
{
  RestorableSnapshot snapshot(world);

  auto dof = skel->getDof(dofIndex);
  dof->setPosition(dof->getPosition() + eps);

  std::shared_ptr<BackpropSnapshot> backpropSnapshot
      = neural::forwardPass(world, true);
  std::shared_ptr<DifferentiableContactConstraint> peerConstraint
      = getPeerConstraint(backpropSnapshot);

  snapshot.restore();

  return peerConstraint->getContactWorldPosition();
}

//==============================================================================
Eigen::Vector3d
DifferentiableContactConstraint::bruteForcePerturbedContactNormal(
    std::shared_ptr<simulation::World> world,
    std::shared_ptr<dynamics::Skeleton> skel,
    int dofIndex,
    double eps)
{
  RestorableSnapshot snapshot(world);

  auto dof = skel->getDof(dofIndex);
  dof->setPosition(dof->getPosition() + eps);

  std::shared_ptr<BackpropSnapshot> backpropSnapshot
      = neural::forwardPass(world, true);
  std::shared_ptr<DifferentiableContactConstraint> peerConstraint
      = getPeerConstraint(backpropSnapshot);

  snapshot.restore();

  return peerConstraint->getContactWorldNormal();
}

//==============================================================================
Eigen::Vector3d
DifferentiableContactConstraint::bruteForcePerturbedContactForceDirection(
    std::shared_ptr<simulation::World> world,
    std::shared_ptr<dynamics::Skeleton> skel,
    int dofIndex,
    double eps)
{
  RestorableSnapshot snapshot(world);

  auto dof = skel->getDof(dofIndex);
  dof->setPosition(dof->getPosition() + eps);

  std::shared_ptr<BackpropSnapshot> backpropSnapshot
      = neural::forwardPass(world, true);
  std::shared_ptr<DifferentiableContactConstraint> peerConstraint
      = getPeerConstraint(backpropSnapshot);

  snapshot.restore();

  return peerConstraint->getContactWorldForceDirection();
}

//==============================================================================
/// Just for testing: This perturbs the world position of a skeleton to read a
/// screw axis will move when rotated by another screw.
Eigen::Vector6d DifferentiableContactConstraint::bruteForceScrewAxis(
    dynamics::DegreeOfFreedom* axis,
    dynamics::DegreeOfFreedom* rotate,
    double eps)
{
  double originalPos = rotate->getPosition();
  rotate->setPosition(originalPos + eps);

  Eigen::Vector6d worldTwist = getWorldScrewAxis(axis);

  rotate->setPosition(originalPos);

  return worldTwist;
}

//==============================================================================
/// Just for testing: This perturbs the world position of a skeleton  to read
/// how edges will move.
EdgeData DifferentiableContactConstraint::bruteForceEdges(
    std::shared_ptr<simulation::World> world,
    std::shared_ptr<dynamics::Skeleton> skel,
    int dofIndex,
    double eps)
{
  EdgeData data;
  data.edgeAPos = Eigen::Vector3d::Zero();
  data.edgeADir = Eigen::Vector3d::Zero();
  data.edgeBPos = Eigen::Vector3d::Zero();
  data.edgeBDir = Eigen::Vector3d::Zero();

  if (getContactType() != collision::ContactType::EDGE_EDGE)
  {
    return data;
  }

  RestorableSnapshot snapshot(world);

  auto dof = skel->getDof(dofIndex);
  dof->setPosition(dof->getPosition() + eps);

  std::shared_ptr<BackpropSnapshot> backpropSnapshot
      = neural::forwardPass(world, true);
  std::shared_ptr<DifferentiableContactConstraint> peerConstraint
      = getPeerConstraint(backpropSnapshot);

  snapshot.restore();

  return peerConstraint->getEdges();
}

//==============================================================================
int DifferentiableContactConstraint::getIndexInConstraint()
{
  return mIndex;
}

//==============================================================================
// This returns 1.0 by default, 0.0 if this constraint doesn't effect the
// specified DOF, and -1.0 if the constraint effects this dof negatively.
double DifferentiableContactConstraint::getForceMultiple(
    dynamics::DegreeOfFreedom* dof)
{
  if (!mConstraint->isContactConstraint())
    return 1.0;

  bool isParentA = isParent(dof, mContactConstraint->getBodyNodeA());
  bool isParentB = isParent(dof, mContactConstraint->getBodyNodeB());

  // This means it's a self-collision, and we're up stream, so the net effect is
  // 0
  if (isParentA && isParentB)
  {
    return 0.0;
  }
  // If we're in skel A
  if (isParentA)
  {
    return 1.0;
  }
  // If we're in skel B
  if (isParentB)
  {
    return -1.0;
  }

  return 0.0;
}

//==============================================================================
bool DifferentiableContactConstraint::isParent(
    const dynamics::DegreeOfFreedom* dof, const dynamics::BodyNode* node)
{
  const dynamics::Joint* dofJoint = dof->getJoint();
  const dynamics::Joint* nodeParentJoint = node->getParentJoint();
  // If these joints aren't in the same skeleton, or aren't in the same tree
  // within that skeleton, this is trivially false
  if (dofJoint->getSkeleton()->getName()
          != nodeParentJoint->getSkeleton()->getName()
      || dofJoint->getTreeIndex() != nodeParentJoint->getTreeIndex())
    return false;
  // If the dof joint is after the node parent joint in the skeleton, this is
  // also false
  if (dofJoint->getIndexInTree(0) > nodeParentJoint->getIndexInTree(0))
    return false;
  // Now this may be true, if the node is a direct child of the dof
  while (true)
  {
    if (nodeParentJoint == dofJoint)
      return true;
    if (nodeParentJoint->getParentBodyNode() == nullptr
        || nodeParentJoint->getParentBodyNode()->getParentJoint() == nullptr)
      return false;
    nodeParentJoint = nodeParentJoint->getParentBodyNode()->getParentJoint();
  }
}

//==============================================================================
bool DifferentiableContactConstraint::isParent(
    const dynamics::DegreeOfFreedom* parent,
    const dynamics::DegreeOfFreedom* child)
{
  const dynamics::Joint* parentJoint = parent->getJoint();
  const dynamics::Joint* childJoint = child->getJoint();
  if (parentJoint == childJoint)
  {
    // For multi-DOF joints, each axis affects all the others.
    return parent->getIndexInJoint() != child->getIndexInJoint();
  }
  // If these joints aren't in the same skeleton, or aren't in the same tree
  // within that skeleton, this is trivially false
  if (parentJoint->getSkeleton()->getName()
          != childJoint->getSkeleton()->getName()
      || parentJoint->getTreeIndex() != childJoint->getTreeIndex())
    return false;
  // If the dof joint is after the node parent joint in the skeleton, this is
  // also false
  if (parentJoint->getIndexInTree(0) > childJoint->getIndexInTree(0))
    return false;
  // Now this may be true, if the node is a direct child of the dof
  while (true)
  {
    if (parentJoint == childJoint)
      return true;
    if (childJoint->getParentBodyNode() == nullptr
        || childJoint->getParentBodyNode()->getParentJoint() == nullptr)
      return false;
    childJoint = childJoint->getParentBodyNode()->getParentJoint();
  }
}

//==============================================================================
Eigen::Vector6d DifferentiableContactConstraint::getWorldScrewAxis(
    std::shared_ptr<dynamics::Skeleton> skel, int dofIndex)
{
  return getWorldScrewAxis(skel->getDof(dofIndex));
}

//==============================================================================
Eigen::Vector6d DifferentiableContactConstraint::getWorldScrewAxis(
    dynamics::DegreeOfFreedom* dof)
{
  int jointIndex = dof->getIndexInJoint();
  math::Jacobian relativeJac = dof->getJoint()->getRelativeJacobian();
  dynamics::BodyNode* childNode = dof->getChildBodyNode();
  Eigen::Isometry3d transform = childNode->getWorldTransform();
  Eigen::Vector6d localTwist = relativeJac.col(jointIndex);
  Eigen::Vector6d worldTwist = math::AdT(transform, localTwist);
  return worldTwist;
}

//==============================================================================
std::shared_ptr<DifferentiableContactConstraint>
DifferentiableContactConstraint::getPeerConstraint(
    std::shared_ptr<neural::BackpropSnapshot> snapshot)
{
  if (mIsUpperBoundConstraint)
  {
    return snapshot->getUpperBoundConstraints()[mOffsetIntoWorld];
  }
  else
  {
    return snapshot->getClampingConstraints()[mOffsetIntoWorld];
  }
}

} // namespace neural
} // namespace dart
