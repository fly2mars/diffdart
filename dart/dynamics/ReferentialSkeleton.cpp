/*
 * Copyright (c) 2015, Georgia Tech Research Corporation
 * All rights reserved.
 *
 * Author(s): Michael X. Grey <mxgrey@gatech.edu>
 *
 * Georgia Tech Graphics Lab and Humanoid Robotics Lab
 *
 * Directed by Prof. C. Karen Liu and Prof. Mike Stilman
 * <karenliu@cc.gatech.edu> <mstilman@cc.gatech.edu>
 *
 * This file is provided under the following "BSD-style" License:
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *   conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *   AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *   POSSIBILITY OF SUCH DAMAGE.
 */

#include "dart/dynamics/ReferentialSkeleton.h"
#include "dart/dynamics/Joint.h"
#include "dart/dynamics/DegreeOfFreedom.h"

namespace dart {
namespace dynamics {

//==============================================================================
const std::string& ReferentialSkeleton::setName(const std::string& _name)
{
  const std::string oldName = mName;
  mName = _name;

  MetaSkeletonPtr me = mPtr.lock();
  mNameChangedSignal.raise(me, oldName, mName);

  return mName;
}

//==============================================================================
const std::string& ReferentialSkeleton::getName() const
{
  return mName;
}

//==============================================================================
size_t ReferentialSkeleton::getNumBodyNodes() const
{
  return mBodyNodes.size();
}

//==============================================================================
template<typename T>
static T getVectorObjectIfAvailable(size_t _idx, const std::vector<T>& _vec)
{
  if (_idx < _vec.size())
    return _vec[_idx];

  assert( _idx < _vec.size() );
  return nullptr;
}

//==============================================================================
BodyNode* ReferentialSkeleton::getBodyNode(size_t _idx)
{
  return getVectorObjectIfAvailable<BodyNodePtr>(_idx, mBodyNodes);
}

//==============================================================================
const BodyNode* ReferentialSkeleton::getBodyNode(size_t _idx) const
{
  return getVectorObjectIfAvailable<BodyNodePtr>(_idx, mBodyNodes);
}

//==============================================================================
template <class T1, class T2>
static std::vector<T2>& convertVector(const std::vector<T1>& t1_vec,
                                      std::vector<T2>& t2_vec)
{
  t2_vec.resize(t1_vec.size());
  for(size_t i = 0; i < t1_vec.size(); ++i)
    t2_vec[i] = t1_vec[i];
  return t2_vec;
}

//==============================================================================
const std::vector<BodyNode*>& ReferentialSkeleton::getBodyNodes()
{
  return convertVector<BodyNodePtr, BodyNode*>(
        mBodyNodes, mRawBodyNodes);
}

//==============================================================================
const std::vector<const BodyNode*>& ReferentialSkeleton::getBodyNodes() const
{
  return convertVector<BodyNodePtr, const BodyNode*>(
        mBodyNodes, mRawConstBodyNodes);
}

//==============================================================================
size_t ReferentialSkeleton::getIndexOf(const BodyNode* _bn, bool _warning) const
{
  if(nullptr == _bn)
  {
    if(_warning)
    {
      dterr << "[ReferentialSkeleton::getIndexOf] Requesting index of a "
            << "nullptr BodyNode!\n";
      assert(false);
    }
    return INVALID_INDEX;
  }

  std::unordered_map<const BodyNode*, IndexMap>::const_iterator it =
      mIndexMap.find(_bn);
  if( it == mIndexMap.end() )
    return INVALID_INDEX;

  return it->second.mBodyNodeIndex;
}

//==============================================================================
size_t ReferentialSkeleton::getNumJoints() const
{
  return mBodyNodes.size();
}

//==============================================================================
Joint* ReferentialSkeleton::getJoint(size_t _idx)
{
  BodyNode* bn = getVectorObjectIfAvailable<BodyNodePtr>(_idx, mBodyNodes);
  if(nullptr == bn)
    return nullptr;

  return bn->getParentJoint();
}

//==============================================================================
const Joint* ReferentialSkeleton::getJoint(size_t _idx) const
{
  const BodyNode* bn = getVectorObjectIfAvailable<BodyNodePtr>(_idx, mBodyNodes);
  if(nullptr == bn)
    return nullptr;

  return bn->getParentJoint();
}

//==============================================================================
size_t ReferentialSkeleton::getIndexOf(const Joint* _joint, bool _warning) const
{
  if(nullptr == _joint)
  {
    if(_warning)
    {
      dterr << "[ReferentialSkeleton::getIndexOf] Requesting index of a nullptr "
            << "Joint!\n";
      assert(false);
    }
    return INVALID_INDEX;
  }

  std::unordered_map<const BodyNode*, IndexMap>::const_iterator it =
      mIndexMap.find(_joint->getChildBodyNode());
  if( it == mIndexMap.end() )
    return INVALID_INDEX;

  return it->second.mBodyNodeIndex;
}

//==============================================================================
size_t ReferentialSkeleton::getNumDofs() const
{
  return mDofs.size();
}

//==============================================================================
DegreeOfFreedom* ReferentialSkeleton::getDof(size_t _idx)
{
  return getVectorObjectIfAvailable<DegreeOfFreedomPtr>(_idx, mDofs);
}

//==============================================================================
const DegreeOfFreedom* ReferentialSkeleton::getDof(size_t _idx) const
{
  return getVectorObjectIfAvailable<DegreeOfFreedomPtr>(_idx, mDofs);
}

//==============================================================================
const std::vector<DegreeOfFreedom*>& ReferentialSkeleton::getDofs()
{
  return convertVector<DegreeOfFreedomPtr, DegreeOfFreedom*>(
        mDofs, mRawDofs);
}

//==============================================================================
std::vector<const DegreeOfFreedom*> ReferentialSkeleton::getDofs() const
{
  return convertVector<DegreeOfFreedomPtr, const DegreeOfFreedom*>(
        mDofs, mRawConstDofs);
}

//==============================================================================
size_t ReferentialSkeleton::getIndexOf(
    const DegreeOfFreedom* _dof, bool _warning) const
{
  if(nullptr == _dof)
  {
    if(_warning)
    {
      dterr << "[ReferentialSkeleton::getIndexOf] Requesting index of a "
            << "nullptr DegreeOfFreedom!\n";
      assert(false);
    }
    return INVALID_INDEX;
  }

  const BodyNode* bn = _dof->getChildBodyNode();
  std::unordered_map<const BodyNode*, IndexMap>::const_iterator it =
      mIndexMap.find(bn);
  if( it == mIndexMap.end() )
    return INVALID_INDEX;

  size_t localIndex = _dof->getIndexInJoint();
  if(it->second.mDofIndices.size() <= localIndex ||
     it->second.mDofIndices[localIndex] == INVALID_INDEX )
  {
    if(_warning)
    {
      dterr << "[ReferentialSkeleton::getIndexOf] BodyNode named ["
            << bn->getName() << "] (" << bn << ") is referenced by the "
            << "ReferentialSkeleton named [" << getName() << "] (" << this
            << "), but it does not include the DegreeOfFreedom #"
            << localIndex << " of its parent Joint!\n";
      assert(false);
    }
    return INVALID_INDEX;
  }

  return it->second.mDofIndices[localIndex];
}

//==============================================================================
static bool isValidBodyNode(const ReferentialSkeleton* /*_refSkel*/,
                            const BodyNode* _bodyNode,
                            const std::string& _fname)
{
  if(nullptr == _bodyNode)
  {
    dtwarn << "[ReferentialSkeleton::" << _fname << "] Invalid BodyNode "
           << "pointer: nullptr. Returning zero Jacobian.\n";
    assert(false);
    return false;
  }

  // We should not test whether the BodyNode is in this ReferentialSkeleton,
  // because even if it is not in this ReferentialSkeleton, it might have
  // dependent degrees of freedom which *are* in this ReferentialSkeleton.

  return true;
}

//==============================================================================
template <typename JacobianType>
void assignJacobian(JacobianType& _J,
                    const ReferentialSkeleton* _refSkel,
                    const BodyNode* _bodyNode,
                    const JacobianType& _JBodyNode)
{
  const std::vector<const DegreeOfFreedom*>& dofs =
      _bodyNode->getDependentDofs();
  size_t nDofs = dofs.size();
  for(size_t i=0; i<nDofs; ++i)
  {
    size_t refIndex = _refSkel->getIndexOf(dofs[i], false);
    if(INVALID_INDEX == refIndex)
      continue;

    _J.col(refIndex) = _JBodyNode.col(i);
  }
}

//==============================================================================
template <typename ...Args>
math::Jacobian variadicGetJacobian(
    const ReferentialSkeleton* _refSkel,
    const BodyNode* _bodyNode,
    Args... args)
{
  math::Jacobian J = math::Jacobian::Zero(6, _refSkel->getNumDofs());

  if( !isValidBodyNode(_refSkel, _bodyNode, "getJacobian") )
    return J;

  const math::Jacobian JBodyNode = _bodyNode->getJacobian(args...);

  assignJacobian(J, _refSkel, _bodyNode, JBodyNode);

  return J;
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getJacobian(const BodyNode* _bodyNode) const
{
  return variadicGetJacobian(this, _bodyNode);
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getJacobian(
    const BodyNode* _bodyNode, const Frame* _inCoordinatesOf) const
{
  return variadicGetJacobian(this, _bodyNode, _inCoordinatesOf);
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getJacobian(
    const BodyNode* _bodyNode, const Eigen::Vector3d& _localOffset) const
{
  return variadicGetJacobian(this, _bodyNode, _localOffset);
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getJacobian(
    const BodyNode* _bodyNode,
    const Eigen::Vector3d& _localOffset,
    const Frame* _inCoordinatesOf) const
{
  return variadicGetJacobian(this, _bodyNode, _localOffset, _inCoordinatesOf);
}

//==============================================================================
template <typename ...Args>
math::Jacobian variadicGetWorldJacobian(const ReferentialSkeleton* _refSkel,
                                        const BodyNode* _bodyNode, Args... args)
{
  math::Jacobian J = math::Jacobian::Zero(6, _refSkel->getNumDofs());

  if( !isValidBodyNode(_refSkel, _bodyNode, "getWorldJacobian") )
    return J;

  const math::Jacobian JBodyNode = _bodyNode->getWorldJacobian(args...);

  assignJacobian(J, _refSkel, _bodyNode, JBodyNode);

  return J;
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getWorldJacobian(
    const BodyNode* _bodyNode) const
{
  return variadicGetWorldJacobian(this, _bodyNode);
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getWorldJacobian(
    const BodyNode* _bodyNode, const Eigen::Vector3d& _localOffset) const
{
  return variadicGetWorldJacobian(this, _bodyNode, _localOffset);
}

//==============================================================================
template <typename ...Args>
math::LinearJacobian variadicGetLinearJacobian(
    const ReferentialSkeleton* _refSkel,
    const BodyNode* _bodyNode, Args... args)
{
  math::LinearJacobian J =
      math::LinearJacobian::Zero(3, _refSkel->getNumDofs());

  if( !isValidBodyNode(_refSkel, _bodyNode, "getLinearJacobian") )
    return J;

  const math::LinearJacobian JBodyNode = _bodyNode->getLinearJacobian(args...);

  assignJacobian(J, _refSkel, _bodyNode, JBodyNode);

  return J;
}

//==============================================================================
math::LinearJacobian ReferentialSkeleton::getLinearJacobian(
    const BodyNode* _bodyNode, const Frame* _inCoordinatesOf) const
{
  return variadicGetLinearJacobian(this, _bodyNode, _inCoordinatesOf);
}

//==============================================================================
math::LinearJacobian ReferentialSkeleton::getLinearJacobian(
    const BodyNode* _bodyNode, const Eigen::Vector3d& _localOffset,
    const Frame* _inCoordinatesOf) const
{
  return variadicGetLinearJacobian(this, _bodyNode, _localOffset,
                                   _inCoordinatesOf);
}

//==============================================================================
template <typename ...Args>
math::AngularJacobian variadicGetAngularJacobian(
    const ReferentialSkeleton* _refSkel,
    const BodyNode* _bodyNode, Args... args)
{
  math::AngularJacobian J =
      math::AngularJacobian::Zero(3, _refSkel->getNumDofs());

  if( !isValidBodyNode(_refSkel, _bodyNode, "getAngularJacobian") )
    return J;

  const math::AngularJacobian JBodyNode =
      _bodyNode->getAngularJacobian(args...);

  assignJacobian(J, _refSkel, _bodyNode, JBodyNode);

  return J;
}

//==============================================================================
math::AngularJacobian ReferentialSkeleton::getAngularJacobian(
    const BodyNode* _bodyNode, const Frame* _inCoordinatesOf) const
{
  return variadicGetAngularJacobian(this, _bodyNode, _inCoordinatesOf);
}

//==============================================================================
template <typename ...Args>
math::Jacobian variadicGetJacobianSpatialDeriv(
    const ReferentialSkeleton* _refSkel,
    const BodyNode* _bodyNode, Args... args)
{
  math::Jacobian dJ = math::Jacobian::Zero(6, _refSkel->getNumDofs());

  if( !isValidBodyNode(_refSkel, _bodyNode, "getJacobianSpatialDeriv") )
    return dJ;

  const math::Jacobian dJBodyNode = _bodyNode->getJacobianSpatialDeriv(args...);

  assignJacobian(dJ, _refSkel, _bodyNode, dJBodyNode);

  return dJ;
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getJacobianSpatialDeriv(
    const BodyNode* _bodyNode) const
{
  return variadicGetJacobianSpatialDeriv(this, _bodyNode);
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getJacobianSpatialDeriv(
    const BodyNode* _bodyNode, const Frame* _inCoordinatesOf) const
{
  return variadicGetJacobianSpatialDeriv(this, _bodyNode, _inCoordinatesOf);
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getJacobianSpatialDeriv(
    const BodyNode* _bodyNode, const Eigen::Vector3d& _localOffset) const
{
  return variadicGetJacobianSpatialDeriv(this, _bodyNode, _localOffset);
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getJacobianSpatialDeriv(
    const BodyNode* _bodyNode, const Eigen::Vector3d& _localOffset,
    const Frame* _inCoordinatesOf) const
{
  return variadicGetJacobianSpatialDeriv(this, _bodyNode, _localOffset,
                                         _inCoordinatesOf);
}

//==============================================================================
template <typename ...Args>
math::Jacobian variadicGetJacobianClassicDeriv(
    const ReferentialSkeleton* _refSkel,
    const BodyNode* _bodyNode, Args... args)
{
  math::Jacobian dJ = math::Jacobian::Zero(6, _refSkel->getNumDofs());

  if( !isValidBodyNode(_refSkel, _bodyNode, "getJacobianClassicDeriv") )
    return dJ;

  const math::Jacobian dJBodyNode = _bodyNode->getJacobianClassicDeriv(args...);

  assignJacobian(dJ, _refSkel, _bodyNode, dJBodyNode);

  return dJ;
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getJacobianClassicDeriv(
    const BodyNode* _bodyNode) const
{
  return variadicGetJacobianClassicDeriv(this, _bodyNode);
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getJacobianClassicDeriv(
    const BodyNode* _bodyNode, const Frame* _inCoordinatesOf) const
{
  return variadicGetJacobianClassicDeriv(this, _bodyNode, _inCoordinatesOf);
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getJacobianClassicDeriv(
    const BodyNode* _bodyNode, const Eigen::Vector3d& _localOffset,
    const Frame* _inCoordinatesOf) const
{
  return variadicGetJacobianClassicDeriv(this, _bodyNode, _localOffset,
                                         _inCoordinatesOf);
}

//==============================================================================
template <typename ...Args>
math::LinearJacobian variadicGetLinearJacobianDeriv(
    const ReferentialSkeleton* _refSkel,
    const BodyNode* _bodyNode, Args... args)
{
  math::LinearJacobian dJv =
      math::LinearJacobian::Zero(3, _refSkel->getNumDofs());

  if( !isValidBodyNode(_refSkel, _bodyNode, "getLinearJacobianDeriv") )
    return dJv;

  const math::LinearJacobian dJvBodyNode =
      _bodyNode->getLinearJacobianDeriv(args...);

  assignJacobian(dJv, _refSkel, _bodyNode, dJvBodyNode);

  return dJv;
}

//==============================================================================
math::LinearJacobian ReferentialSkeleton::getLinearJacobianDeriv(
    const BodyNode* _bodyNode, const Frame* _inCoordinatesOf) const
{
  return variadicGetLinearJacobianDeriv(this, _bodyNode, _inCoordinatesOf);
}

//==============================================================================
math::LinearJacobian ReferentialSkeleton::getLinearJacobianDeriv(
    const BodyNode* _bodyNode, const Eigen::Vector3d& _localOffset,
    const Frame* _inCoordinatesOf) const
{
  return variadicGetLinearJacobianDeriv(this, _bodyNode, _localOffset,
                                        _inCoordinatesOf);
}

//==============================================================================
template <typename ...Args>
math::AngularJacobian variadicGetAngularJacobianDeriv(
    const ReferentialSkeleton* _refSkel,
    const BodyNode* _bodyNode, Args... args)
{
  math::AngularJacobian dJw =
      math::AngularJacobian::Zero(3, _refSkel->getNumDofs());

  if( !isValidBodyNode(_refSkel, _bodyNode, "getAngularJacobianDeriv") )
    return dJw;

  const math::AngularJacobian dJwBodyNode =
      _bodyNode->getAngularJacobianDeriv(args...);

  assignJacobian(dJw, _refSkel, _bodyNode, dJwBodyNode);

  return dJw;
}

//==============================================================================
math::AngularJacobian ReferentialSkeleton::getAngularJacobianDeriv(
    const BodyNode* _bodyNode, const Frame* _inCoordinatesOf) const
{
  return variadicGetAngularJacobianDeriv(this, _bodyNode, _inCoordinatesOf);
}

//==============================================================================
double ReferentialSkeleton::getMass() const
{
  double mass = 0;
  for(const BodyNode* bn : mRawBodyNodes)
    mass += bn->getMass();

  return mass;
}

//==============================================================================
template <const Eigen::MatrixXd& (Skeleton::*getMatrix)(size_t) const>
const Eigen::MatrixXd& setMatrixFromSkeletonData(
    Eigen::MatrixXd& M, const std::vector<const DegreeOfFreedom*>& dofs)
{
  const size_t nDofs = dofs.size();

  M = Eigen::MatrixXd::Zero(nDofs, nDofs);

  for(size_t i=0; i<nDofs; ++i)
  {
    const DegreeOfFreedom* dof_i = dofs[i];
    const size_t tree_i = dof_i->getTreeIndex();
    const ConstSkeletonPtr skel_i = dof_i->getSkeleton();

    const size_t index_i = dof_i->getIndexInTree();
    const Eigen::MatrixXd& treeMatrix = (skel_i.get()->*getMatrix)(tree_i);

    M(i,i) = treeMatrix(index_i, index_i);

    for(size_t j=i+1; j<nDofs; ++j)
    {
      const DegreeOfFreedom* dof_j = dofs[j];
      const size_t tree_j = dof_j->getTreeIndex();
      const ConstSkeletonPtr skel_j = dof_j->getSkeleton();

      // If the DegreesOfFreedom are in the same tree within the same
      // Skeleton, then set their entries in the referential matrix.
      // Otherwise, leave the entry as zero.
      if(skel_i == skel_j && tree_i == tree_j)
      {
        const size_t index_j = dof_j->getIndexInTree();

        M(i,j) = treeMatrix(index_i, index_j);
        M(j,i) = M(i,j);
      }
    }
  }

  return M;
}

//==============================================================================
const Eigen::MatrixXd& ReferentialSkeleton::getMassMatrix() const
{
  return setMatrixFromSkeletonData<&Skeleton::getMassMatrix>(mM, mRawConstDofs);
}

//==============================================================================
const Eigen::MatrixXd& ReferentialSkeleton::getAugMassMatrix() const
{
  return setMatrixFromSkeletonData<&Skeleton::getAugMassMatrix>(
        mAugM, mRawConstDofs);
}

//==============================================================================
const Eigen::MatrixXd& ReferentialSkeleton::getInvMassMatrix() const
{
  return setMatrixFromSkeletonData<&Skeleton::getInvMassMatrix>(
        mInvM, mRawConstDofs);
}

//==============================================================================
const Eigen::MatrixXd& ReferentialSkeleton::getInvAugMassMatrix() const
{
  return setMatrixFromSkeletonData<&Skeleton::getInvAugMassMatrix>(
        mInvAugM, mRawConstDofs);
}

//==============================================================================
template <const Eigen::VectorXd& (Skeleton::*getVector)(size_t) const>
const Eigen::VectorXd& setVectorFromSkeletonData(
    Eigen::VectorXd& V, const std::vector<const DegreeOfFreedom*>& dofs)
{
  const size_t nDofs = dofs.size();

  V = Eigen::VectorXd::Zero(nDofs);

  for(size_t i=0; i<nDofs; ++i)
  {
    const DegreeOfFreedom* dof_i = dofs[i];
    const size_t tree_i = dof_i->getTreeIndex();
    const ConstSkeletonPtr skel_i = dof_i->getSkeleton();

    const size_t index_i = dof_i->getIndexInTree();
    const Eigen::VectorXd& treeVector = (skel_i.get()->*getVector)(tree_i);

    V[i] = treeVector[index_i];
  }

  return V;
}

//==============================================================================
const Eigen::VectorXd& ReferentialSkeleton::getCoriolisForces() const
{
  return setVectorFromSkeletonData<&Skeleton::getCoriolisForces>(
        mCvec, mRawConstDofs);
}

//==============================================================================
const Eigen::VectorXd& ReferentialSkeleton::getGravityForces() const
{
  return setVectorFromSkeletonData<&Skeleton::getGravityForces>(
        mG, mRawConstDofs);
}

//==============================================================================
const Eigen::VectorXd& ReferentialSkeleton::getCoriolisAndGravityForces() const
{
  return setVectorFromSkeletonData<&Skeleton::getCoriolisAndGravityForces>(
        mCg, mRawConstDofs);
}

//==============================================================================
const Eigen::VectorXd& ReferentialSkeleton::getExternalForces() const
{
  return setVectorFromSkeletonData<&Skeleton::getExternalForces>(
        mFext, mRawConstDofs);
}

//==============================================================================
const Eigen::VectorXd& ReferentialSkeleton::getConstraintForces() const
{
  return setVectorFromSkeletonData<&Skeleton::getConstraintForces>(
        mFc, mRawConstDofs);
}

//==============================================================================
void ReferentialSkeleton::clearExternalForces()
{
  for(BodyNode* bn : mRawBodyNodes)
    bn->clearExternalForces();
}

//==============================================================================
void ReferentialSkeleton::clearInternalForces()
{
  for(BodyNode* bn : mRawBodyNodes)
    bn->clearInternalForces();
}

//==============================================================================
double ReferentialSkeleton::getKineticEnergy() const
{
  double KE = 0.0;

  for(const BodyNode* bn : mRawBodyNodes)
    KE += bn->getKineticEnergy();

  assert( KE >= 0.0 && "Kinetic Energy should always be zero or greater");
  return KE;
}

//==============================================================================
double ReferentialSkeleton::getPotentialEnergy() const
{
  double PE = 0.0;

  for(const BodyNode* bn : mRawBodyNodes)
  {
    PE += bn->getPotentialEnergy(bn->getSkeleton()->getGravity());
    PE += bn->getParentJoint()->getPotentialEnergy();
  }

  return PE;
}

//==============================================================================
Eigen::Vector3d ReferentialSkeleton::getCOM(const Frame* _withRespectTo) const
{
  Eigen::Vector3d com = Eigen::Vector3d::Zero();
  double totalMass = 0.0;

  for(const BodyNode* bn : mRawConstBodyNodes)
  {
    com += bn->getMass() * bn->getCOM(_withRespectTo);
    totalMass += bn->getMass();
  }

  assert(totalMass != 0.0);
  return com / totalMass;
}

//==============================================================================
// Templated function for computing different kinds of COM properties, like
// velocities and accelerations
template <
    typename PropertyType,
    PropertyType (BodyNode::*getProperty)(const Frame*, const Frame*) const>
PropertyType getCOMPropertyTemplate(const ReferentialSkeleton* _refSkel,
                                    const Frame* _relativeTo,
                                    const Frame* _inCoordinatesOf)
{
  PropertyType result = PropertyType::Zero();
  double totalMass = 0.0;

  const std::vector<const BodyNode*>& bodyNodes = _refSkel->getBodyNodes();
  for(const BodyNode* bn : bodyNodes)
  {
    result += bn->getMass() * (bn->*getProperty)(_relativeTo, _inCoordinatesOf);
    totalMass += bn->getMass();
  }

  assert(totalMass != 0.0);
  return result / totalMass;
}

//==============================================================================
Eigen::Vector6d ReferentialSkeleton::getCOMSpatialVelocity(
    const Frame* _relativeTo, const Frame* _inCoordinatesOf) const
{
  return getCOMPropertyTemplate<Eigen::Vector6d,
      &BodyNode::getCOMSpatialVelocity>(this, _relativeTo, _inCoordinatesOf);
}

//==============================================================================
Eigen::Vector3d ReferentialSkeleton::getCOMLinearVelocity(
    const Frame* _relativeTo, const Frame* _inCoordinatesOf) const
{
  return getCOMPropertyTemplate<Eigen::Vector3d,
      &BodyNode::getCOMLinearVelocity>(this, _relativeTo, _inCoordinatesOf);
}

//==============================================================================
Eigen::Vector6d ReferentialSkeleton::getCOMSpatialAcceleration(
    const Frame* _relativeTo, const Frame* _inCoordinatesOf) const
{
  return getCOMPropertyTemplate<Eigen::Vector6d,
      &BodyNode::getCOMSpatialAcceleration>(
        this, _relativeTo, _inCoordinatesOf);
}

//==============================================================================
Eigen::Vector3d ReferentialSkeleton::getCOMLinearAcceleration(
    const Frame* _relativeTo, const Frame* _inCoordinatesOf) const
{
  return getCOMPropertyTemplate<Eigen::Vector3d,
      &BodyNode::getCOMLinearAcceleration>(this, _relativeTo, _inCoordinatesOf);
}

//==============================================================================
// Templated function for computing different kinds of COM Jacobians and their
// derivatives
template <
    typename JacType, // JacType is the type of Jacobian we're computing
    JacType (BodyNode::*getJacFn)(const Eigen::Vector3d&, const Frame*) const>
JacType getCOMJacobianTemplate(const ReferentialSkeleton* _refSkel,
                               const Frame* _inCoordinatesOf)
{
  // Initialize the Jacobian to zero
  JacType J = JacType::Zero(JacType::RowsAtCompileTime, _refSkel->getNumDofs());
  double totalMass = 0.0;

  // Iterate through each of the BodyNodes
  const std::vector<const BodyNode*>& bodyNodes = _refSkel->getBodyNodes();
  for(const BodyNode* bn : bodyNodes)
  {
    JacType bnJ = bn->getMass() * (bn->*getJacFn)(bn->getLocalCOM(),
                                                  _inCoordinatesOf);
    totalMass += bn->getMass();

    const std::vector<const DegreeOfFreedom*>& dofs = bn->getDependentDofs();
    size_t nDofs = dofs.size();
    for(size_t i=0; i<nDofs; ++i)
    {
      const DegreeOfFreedom* dof = dofs[i];
      size_t index = _refSkel->getIndexOf(dof, false);
      if(INVALID_INDEX == index)
        continue;

      J.col(index) += bnJ.col(i);
    }
  }

  assert(totalMass != 0.0);
  return J / totalMass;
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getCOMJacobian(
    const Frame* _inCoordinatesOf) const
{
  return getCOMJacobianTemplate<
      math::Jacobian, &BodyNode::getJacobian>(
        this, _inCoordinatesOf);
}

//==============================================================================
math::LinearJacobian ReferentialSkeleton::getCOMLinearJacobian(
    const Frame* _inCoordinatesOf) const
{
  return getCOMJacobianTemplate<
      math::LinearJacobian, &BodyNode::getLinearJacobian>(
        this, _inCoordinatesOf);
}

//==============================================================================
math::Jacobian ReferentialSkeleton::getCOMJacobianSpatialDeriv(
    const Frame* _inCoordinatesOf) const
{
  return getCOMJacobianTemplate<
      math::Jacobian, &BodyNode::getJacobianSpatialDeriv>(
        this, _inCoordinatesOf);
}

//==============================================================================
math::LinearJacobian ReferentialSkeleton::getCOMLinearJacobianDeriv(
    const Frame* _inCoordinatesOf) const
{
  return getCOMJacobianTemplate<
      math::LinearJacobian, &BodyNode::getLinearJacobianDeriv>(
        this, _inCoordinatesOf);
}

//==============================================================================
void ReferentialSkeleton::registerBodyNode(BodyNode* _bn)
{
  size_t nDofs = _bn->getParentJoint()->getNumDofs();
  for(size_t i=0; i < nDofs; ++i)
  {
    registerDegreeOfFreedom(_bn->getParentJoint()->getDof(i));
  }
}

//==============================================================================
void ReferentialSkeleton::registerDegreeOfFreedom(DegreeOfFreedom* _dof)
{
  BodyNode* bn = _dof->getChildBodyNode();
  size_t localIndex = _dof->getIndexInJoint();

  std::unordered_map<const BodyNode*, IndexMap>::iterator it =
      mIndexMap.find(bn);

  if( it == mIndexMap.end() )
  {
    IndexMap indexing;
    mBodyNodes.push_back(bn);
    indexing.mBodyNodeIndex = mBodyNodes.size()-1;
    mBodyNodes.push_back(bn);

    indexing.mDofIndices.resize(localIndex+1, INVALID_INDEX);
    mDofs.push_back(_dof);
    indexing.mDofIndices[localIndex] = mDofs.size()-1;
  }
  else
  {
    IndexMap& indexing = it->second;
    if(indexing.mDofIndices.size() < localIndex+1)
      indexing.mDofIndices.resize(localIndex+1, INVALID_INDEX);
    mDofs.push_back(_dof);
    indexing.mDofIndices[localIndex] = mDofs.size()-1;
  }
}

//==============================================================================
void ReferentialSkeleton::unregisterBodyNode(BodyNode* _bn)
{
  if(nullptr == _bn)
  {
    dterr << "[ReferentialSkeleton::unregisterBodyNode] Attempting to "
          << "unregister a nullptr BodyNode. This is most likely a bug. Please "
          << "report this!\n";
    assert(false);
    return;
  }

  std::unordered_map<const BodyNode*, IndexMap>::iterator it =
      mIndexMap.find(_bn);

  if( it == mIndexMap.end() )
  {
    dterr << "[ReferentialSkeleton::unregisterBodyNode] Attempting to "
          << "unregister a BodyNode that is not referred to by this "
          << "ReferentialSkeleton. This is most likely a bug. Please report "
          << "this!\n";
    assert(false);
    return;
  }

  const IndexMap& indexing = it->second;

  for(size_t i=0; i<indexing.mDofIndices.size(); ++i)
  {
    if(indexing.mDofIndices[i] != INVALID_INDEX)
      unregisterDegreeOfFreedom(_bn, i, false);
  }

  size_t bnIndex = indexing.mBodyNodeIndex;
  mBodyNodes.erase(mBodyNodes.begin() + indexing.mBodyNodeIndex);
  for(size_t i = bnIndex; i < mBodyNodes.size(); ++i)
  {
    IndexMap& indexing = mIndexMap[mBodyNodes[i]];
    indexing.mBodyNodeIndex = i;
  }
  mIndexMap.erase(it);
}

//==============================================================================
void ReferentialSkeleton::unregisterDegreeOfFreedom(
    BodyNode* _bn, size_t _localIndex, bool removeBnIfEmpty)
{
  if(nullptr == _bn)
  {
    dterr << "[ReferentialSkeleton::unregisterDegreeOfFreedom] Attempting to "
          << "unregister a DegreeOfFreedom from a nullptr BodyNode. This is "
          << "most likely a bug. Please report this!\n";
    assert(false);
    return;
  }

  std::unordered_map<const BodyNode*, IndexMap>::iterator it =
      mIndexMap.find(_bn);

  if( it == mIndexMap.end() ||
      it->second.mDofIndices.size() <= _localIndex ||
      it->second.mDofIndices[_localIndex] == INVALID_INDEX)
  {
    dterr << "[ReferentialSkeleton::unregisterDegreeOfFreedom] Attempting to "
          << "unregister a DegreeOfFreedom from a BodyNode named ["
          << _bn->getName() << "] (" << _bn << ") that is not currently in the "
          << "ReferentialSkeleton! This is most likely a bug. Please report "
          << "this!\n";
    assert(false);
    return;
  }

  size_t dofIndex = it->second.mDofIndices[_localIndex];
  mDofs.erase(mDofs.begin() + dofIndex);
  it->second.mDofIndices[_localIndex] = INVALID_INDEX;

  for(size_t i = dofIndex; i < mDofs.size(); ++i)
  {
    DegreeOfFreedomPtr dof = mDofs[i];
    IndexMap& indexing = mIndexMap[dof.getBodyNodePtr()];
    indexing.mDofIndices[dof.getLocalIndex()] = i;
  }

  if(removeBnIfEmpty)
  {
    const std::vector<size_t>& dofIndices = it->second.mDofIndices;
    bool removeBn = true;
    for(size_t i=0; i<dofIndices.size(); ++i)
    {
      if(dofIndices[i] != INVALID_INDEX)
      {
        removeBn = false;
        break;
      }
    }

    if(removeBn)
      unregisterBodyNode(_bn);
  }
}

} // namespace dynamics
} // namespace dart
