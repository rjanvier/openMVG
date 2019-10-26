// This file is part of OpenMVG, an Open Multiple View Geometry C++ library.

// Copyright (c) 2016 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "openMVG/multiview/projection.hpp"
#include "openMVG/multiview/triangulation.hpp"
#include "openMVG/numeric/numeric.h"

namespace openMVG {

// HZ 12.2 pag.312
void TriangulateDLT
(
  const Mat34 &P1,
  const Vec3 &x1,
  const Mat34 &P2,
  const Vec3 &x2,
  Vec4 *X_homogeneous
)
{
  // Solve:
  // [cross(x0,P0) X = 0]
  // [cross(x1,P1) X = 0]
  Mat4 design;
  design.row(0) = x1[0] * P1.row(2) - x1[2] * P1.row(0);
  design.row(1) = x1[1] * P1.row(2) - x1[2] * P1.row(1);
  design.row(2) = x2[0] * P2.row(2) - x2[2] * P2.row(0);
  design.row(3) = x2[1] * P2.row(2) - x2[2] * P2.row(1);

  Eigen::JacobiSVD<Mat4> svd( design, Eigen::ComputeFullV );
  ( *X_homogeneous ) = svd.matrixV().col( 3 );
}

void TriangulateDLT
(
  const Mat34 &P1,
  const Vec3 &x1,
  const Mat34 &P2,
  const Vec3 &x2,
  Vec3 *X_euclidean
)
{
  Vec4 X_homogeneous;
  TriangulateDLT(P1, x1, P2, x2, &X_homogeneous);
  (*X_euclidean) = X_homogeneous.hnormalized();
}

void TriangulateL1Angular
(
  const Mat3 &R0,
  const Vec3 &t0,
  const Vec3 &x0,
  const Mat3 &R1,
  const Vec3 &t1,
  const Vec3 &x1,
  Vec3 *X_euclidean
)
{
  // Table 1 - 1) we compute m0 and m1
  // absolute to relative
  const Mat3 R = R1 * R0.transpose();
  const Vec3 t = t1 - R * t0;

  const Vec3 m0 = R * x0;
  const Vec3 & m1 = x1;

  // Table 1 -2) obtain m'0 and m'1
  // allocate the two vectors
  Vec3 mprime0;
  Vec3 mprime1;

  // pre compute n0 and n1 cf. 5. Lemma 2
  const Vec3 n0 = m0.cross(t).normalized();
  const Vec3 n1 = m1.cross(t).normalized();
  
  if(m0.normalized().cross(t).norm() <= m1.normalized().cross(t).norm())
  {
    // Eq. (12)
    mprime0 = m0 - m0.dot(n1) * n1;
    mprime1 = m1;
  } 
  else 
  {
    // Eq. (13)
    mprime0 = m0;
    mprime1 = m1 - m1.dot(n0) * n0;
  }

  // Table 1 - 3)
  // Rf'0 = m'0 and f'1 = m'1
  // Eq. (11)
  const Vec3 z = mprime1.cross(mprime0);
  const Vec3 xprime1 = t + (z.dot(t.cross(mprime1)) / z.squaredNorm()) * mprime0;
  // x'1 is into the frame of camera1 convert it into the world frame in order to obtain the 3D point
  *X_euclidean = R1.transpose() * (xprime1 - t1);
}

void TriangulateLInfinityAngular
(
  const Mat3 &R0,
  const Vec3 &t0,
  const Vec3 &x0,
  const Mat3 &R1,
  const Vec3 &t1,
  const Vec3 &x1,
  Vec3 *X_euclidean
)
{
  // Table 1 - 1) we compute m0 and m1
  // absolute to relative
  const Mat3 R = R1 * R0.transpose();
  const Vec3 t = t1 - R * t0;

  const Vec3 m0 = R * x0;
  const Vec3 & m1 = x1;

  //cf. 7. Lemma 2
  const Vec3 na = (m0.normalized() + m1.normalized()).cross(t);
  const Vec3 nb = (m0.normalized() - m1.normalized()).cross(t);

  const Vec3 & nprime = na.norm() >= nb.norm() ? na.normalized() : nb.normalized();

  const Vec3 mprime0 = m0 - (m0.dot(nprime)) * nprime;
  const Vec3 mprime1 = m1 - (m1.dot(nprime)) * nprime;

  // Table 1 - 3)
  // Rf'0 = m'0 and f'1 = m'1
  // Eq. (11)
  const Vec3 z = mprime1.cross(mprime0);
  const Vec3 xprime1 = t + (z.dot(t.cross(mprime1)) / z.squaredNorm()) * mprime0;
  // x'1 is into the frame of camera1 convert it into the world frame in order to obtain the 3D point
  *X_euclidean = R1.transpose() * (xprime1 - t1);
}

bool TriangulateIDW(
  const Mat34& P1,
  const Vec3& x1,
  const Mat34& P2,
  const Vec3& x2,
  Vec3* X_euclidean
) {
  // x1 && x2 are bearings, thus they should be should be normalized
  /*const Vec3 x1_norm = x1.normalized();
  const Vec3 x2_norm = x2.normalized();*/
  const Vec3& x1_norm = x1;
  const Vec3& x2_norm = x2;

  const Mat3 &R1 = P1.block<3, 3>(0, 0);
  const Mat3 &R2 = P2.block<3, 3>(0, 0);
  const Vec3 &t1 = P1.block<3, 1>(0, 3);
  const Vec3 &t2 = P2.block<3, 1>(0, 3);
    
  // absolute to relative
  const Mat3 R = R2 * R1.transpose();
  const Vec3 t = t2 - R * t1;

  const Vec3 Rx1_nom = R * x1_norm;

  const double p_norm = Rx1_nom.cross(x2_norm).norm();
  const double q_norm = Rx1_nom.cross(t).norm();
  const double r_norm = x2_norm.cross(t).norm();
  
  //Eq. (10)
  const auto xprime1 = ( q_norm / (q_norm + r_norm) ) 
    * ( t + (r_norm / p_norm) * (Rx1_nom + x2_norm) );

   // Relative to absolute
   *X_euclidean = R2.transpose() * (xprime1 - t2);

   //Eq. (7)
   const Vec3 lambda_1_Rx1_nom = (r_norm / p_norm) * Rx1_nom ;
   const Vec3 lambda_2_x2_norm = (q_norm / p_norm) * x2_norm;

   //Eq. (9) - Cheirality 
   return (t + lambda_1_Rx1_nom - lambda_2_x2_norm).squaredNorm()
     <
     std::min(std::min(
      (t + lambda_1_Rx1_nom + lambda_2_x2_norm).squaredNorm(),
      (t - lambda_1_Rx1_nom - lambda_2_x2_norm).squaredNorm()),
      (t - lambda_1_Rx1_nom + lambda_2_x2_norm).squaredNorm());
}

}  // namespace openMVG
