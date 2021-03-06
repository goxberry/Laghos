// Copyright (c) 2017, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-734707. All Rights
// reserved. See files LICENSE and NOTICE for details.
//
// This file is part of CEED, a collection of benchmarks, miniapps, software
// libraries and APIs for efficient high-order finite element and spectral
// element discretizations for exascale applications. For more information and
// source code availability see http://github.com/ceed.
//
// The CEED research is supported by the Exascale Computing Project 17-SC-20-SC,
// a collaborative effort of two U.S. Department of Energy organizations (Office
// of Science and the National Nuclear Security Administration) responsible for
// the planning and preparation of a capable exascale ecosystem, including
// software, applications, hardware, advanced system engineering and early
// testbed platforms, in support of the nation's exascale computing imperative.

#ifndef MFEM_LAGHOS_ASSEMBLY
#define MFEM_LAGHOS_ASSEMBLY

#include "mfem.hpp"
#include "hip/hip.hpp"

#ifdef MFEM_USE_MPI

#include <memory>
#include <iostream>

namespace mfem
{

namespace hydrodynamics
{

// Container for all data needed at quadrature points.
struct QuadratureData
{
   // TODO: use QuadratureFunctions?

   // Reference to physical Jacobian for the initial mesh. These are computed
   // only at time zero and stored here.
   HipVector Jac0inv;

   // Quadrature data used for full/partial assembly of the force operator. At
   // each quadrature point, it combines the stress, inverse Jacobian,
   // determinant of the Jacobian and the integration weight. It must be
   // recomputed in every time step.
   HipVector stressJinvT;
   HipDofQuadMaps *dqMaps;
   HipGeometry *geom;

   // Quadrature data used for full/partial assembly of the mass matrices. At
   // time zero, we compute and store (rho0 * det(J0) * qp_weight) at each
   // quadrature point. Note the at any other time, we can compute
   // rho = rho0 * det(J0) / det(J), representing the notion of pointwise mass
   // conservation.
   HipVector rho0DetJ0w;


   // Initial length scale. This represents a notion of local mesh size. We
   // assume that all initial zones have similar size.
   double h0;

   // Estimate of the minimum time step over all quadrature points. This is
   // recomputed at every time step to achieve adaptive time stepping.
   double dt_est;
   HipVector dtEst;

   QuadratureData(int dim, int nzones, int quads_per_zone);

   void Setup(int dim, int nzones, int quads_per_zone);
};

// This class is used only for visualization. It assembles (rho, phi) in each
// zone, which is used by LagrangianHydroOperator::ComputeDensity to do an L2
// projection of the density.
class DensityIntegrator : public LinearFormIntegrator
{
private:
   const QuadratureData &quad_data;
   const IntegrationRule &integ_rule;
public:
   DensityIntegrator(const QuadratureData &qd,
                     const IntegrationRule &ir) : quad_data(qd),
      integ_rule(ir) {}

   void AssembleRHSElementVect(const FiniteElement &fe,
                               ElementTransformation &Tr,
                               Vector &elvect);

   void AssembleRHSElementVect(const FiniteElement &el,
                               FaceElementTransformations &Tr,
                               Vector &elvect) {assert(false);}

};

// *****************************************************************************
// * HipMassOperator
// *****************************************************************************
class HipMassOperator : public HipOperator
{
private:
   int dim;
   int nzones;
   HipFiniteElementSpace &fes;
   const IntegrationRule &integ_rule;
   unsigned int ess_tdofs_count;
   HipArray<int> ess_tdofs;
   HipBilinearForm bilinearForm;
   HipOperator *massOperator;
   QuadratureData *quad_data;
   // For distributing X
   mutable HipVector distX;
   mutable HipGridFunction x_gf, y_gf;
public:
   HipMassOperator(HipFiniteElementSpace &fes_,
                    const IntegrationRule &integ_rule_,
                    QuadratureData *quad_data_);
   ~HipMassOperator();
   void Setup();
   void SetEssentialTrueDofs(Array<int> &dofs);
   // Can be used for both velocity and specific internal energy. For the case
   // of velocity, we only work with one component at a time.
   void Mult(const HipVector &x, HipVector &y) const;
   void EliminateRHS(HipVector &b);
   void ComputeDiagonal2D(Vector &diag) const;
   void ComputeDiagonal3D(Vector &diag) const;
};

// Performs partial assembly, which corresponds to (and replaces) the use of the
// LagrangianHydroOperator::Force global matrix.
class HipForceOperator : public HipOperator
{
private:
   const int dim;
   const int nzones;
   const HipFiniteElementSpace &h1fes, &l2fes;
   const IntegrationRule &integ_rule;
   const QuadratureData *quad_data;
   const HipDofQuadMaps *l2D2Q, *h1D2Q;
   mutable HipVector gVecL2, gVecH1;
public:
   HipForceOperator(HipFiniteElementSpace &h1fes_,
                     HipFiniteElementSpace &l2fes_,
                     const IntegrationRule &integ_rule,
                     const QuadratureData *quad_data_);
   void Setup();
   void Mult(const HipVector &vecL2, HipVector &vecH1) const;
   void MultTranspose(const HipVector &vecH1, HipVector &vecL2) const;
   ~HipForceOperator();
};

// Scales by the inverse diagonal of the MassPAOperator.
class DiagonalSolver : public Solver
{
private:
   Vector diag;
   FiniteElementSpace &FESpace;
public:
   DiagonalSolver(FiniteElementSpace &fes): Solver(fes.GetVSize()),
      diag(),
      FESpace(fes) { }

   void SetDiagonal(Vector &d)
   {
      const Operator *P = FESpace.GetProlongationMatrix();
      diag.SetSize(P->Width());
      P->MultTranspose(d, diag);
   }

   virtual void Mult(const Vector &x, Vector &y) const
   {
      for (int i = 0; i < x.Size(); i++) { y(i) = x(i) / diag(i); }
   }
   virtual void SetOperator(const Operator &op) { }
};

} // namespace hydrodynamics

} // namespace mfem

#endif // MFEM_USE_MPI

#endif // MFEM_LAGHOS_ASSEMBLY
