///
/// Copyright (c) 2013, Intel Corporation
///
/// Redistribution and use in source and binary forms, with or without
/// modification, are permitted provided that the following conditions
/// are met:
///
/// * Redistributions of source code must retain the above copyright
///       notice, this list of conditions and the following disclaimer.
/// * Redistributions in binary form must reproduce the above
///       copyright notice, this list of conditions and the following
///       disclaimer in the documentation and/or other materials provided
///       with the distribution.
/// * Neither the name of Intel Corporation nor the names of its
///       contributors may be used to endorse or promote products
///       derived from this software without specific prior written
///       permission.
///
/// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
/// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
/// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
/// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
/// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
/// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
/// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
/// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
/// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
/// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
/// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
/// POSSIBILITY OF SUCH DAMAGE.

//////////////////////////////////////////////////////////////////////
///
/// NAME:    transpose
///
/// PURPOSE: This program measures the time for the transpose of a
///          column-major stored matrix into a row-major stored matrix.
///
/// USAGE:   Program input is the matrix order and the number of times to
///          repeat the operation:
///
///          transpose <matrix_size> <# iterations> [tile size]
///
///          An optional parameter specifies the tile size used to divide the
///          individual matrix blocks for improved cache and TLB performance.
///
///          The output consists of diagnostics to make sure the
///          transpose worked and timing statistics.
///
/// HISTORY: Written by  Rob Van der Wijngaart, February 2009.
///          Converted to C++11 by Jeff Hammond, February 2016 and May 2017.
///
//////////////////////////////////////////////////////////////////////

#include "prk_util.h"

struct Initialize
{
    public:
        void operator()( const tbb::blocked_range2d<int>& r ) const {
            for (tbb::blocked_range<int>::const_iterator i=r.rows().begin(); i!=r.rows().end(); ++i ) {
                for (tbb::blocked_range<int>::const_iterator j=r.cols().begin(); j!=r.cols().end(); ++j ) {
                    A_[i*n_+j] = static_cast<double>(i*n_+j);
                    B_[i*n_+j] = 0.0;
                }
            }
        }

        Initialize(int n, std::vector<double> & A, std::vector<double> & B) : n_(n), A_(A), B_(B) { }

    private:
        int n_;
        std::vector<double> & A_;
        std::vector<double> & B_;

};

struct Transpose
{
    public:
        void operator()( const tbb::blocked_range2d<int>& r ) const {
            for (tbb::blocked_range<int>::const_iterator i=r.rows().begin(); i!=r.rows().end(); ++i ) {
                for (tbb::blocked_range<int>::const_iterator j=r.cols().begin(); j!=r.cols().end(); ++j ) {
                    B_[i*n_+j] += A_[j*n_+i];
                    A_[j*n_+i] += 1.0;
                }
            }
        }

        Transpose(int n, std::vector<double> & A, std::vector<double> & B) : n_(n), A_(A), B_(B) { }

    private:
        int n_;
        std::vector<double> & A_;
        std::vector<double> & B_;

};

void ParallelInitialize(int order, int tile_size, std::vector<double> & A, std::vector<double> & B)
{
    Initialize t(order, A, B);
    const tbb::blocked_range2d<int> r(0, order, tile_size, 0, order, tile_size);
    parallel_for(r,t);
}

void ParallelTranspose(int order, int tile_size, std::vector<double> & A, std::vector<double> & B)
{
    Transpose t(order, A, B);
    const tbb::blocked_range2d<int> r(0, order, tile_size, 0, order, tile_size);
    parallel_for(r,t);
}

int main(int argc, char * argv[])
{
  std::cout << "Parallel Research Kernels version " << PRKVERSION << std::endl;
  std::cout << "C++11/TBB Matrix transpose: B = A^T" << std::endl;

  //////////////////////////////////////////////////////////////////////
  /// Read and test input parameters
  //////////////////////////////////////////////////////////////////////

  int iterations;
  int order;
  int tile_size;
  try {
      if (argc < 3) {
        throw "Usage: <# iterations> <matrix order> [tile size]";
      }

      // number of times to do the transpose
      iterations  = std::atoi(argv[1]);
      if (iterations < 1) {
        throw "ERROR: iterations must be >= 1";
      }

      // order of a the matrix
      order = std::atol(argv[2]);
      if (order <= 0) {
        throw "ERROR: Matrix Order must be greater than 0";
      }

      // default tile size for tiling of local transpose
      tile_size = (argc>3) ? std::atol(argv[3]) : 32;
      // a negative tile size means no tiling of the local transpose
      if (tile_size <= 0) tile_size = order;
  }
  catch (const char * e) {
    std::cout << e << std::endl;
    return 1;
  }

  std::cout << "Number of iterations  = " << iterations << std::endl;
  std::cout << "Matrix order          = " << order << std::endl;
  if (tile_size < order) {
      std::cout << "Tile size             = " << tile_size << std::endl;
  } else {
      std::cout << "Untiled" << std::endl;
  }

  tbb::task_scheduler_init init(tbb::task_scheduler_init::automatic);

  //////////////////////////////////////////////////////////////////////
  /// Allocate space for the input and transpose matrix
  //////////////////////////////////////////////////////////////////////

  std::vector<double> A;
  std::vector<double> B;
  A.resize(order*order);
  B.resize(order*order);

  auto trans_time = 0.0;

  ParallelInitialize(order, tile_size, A, B);

  for (auto iter = 0; iter<=iterations; iter++) {
    if (iter==1) trans_time = prk::wtime();
    ParallelTranspose(order, tile_size, A, B);
  }
  trans_time = prk::wtime() - trans_time;

  //////////////////////////////////////////////////////////////////////
  /// Analyze and output results
  //////////////////////////////////////////////////////////////////////

  const auto addit = (iterations+1.) * (iterations/2.);
  auto abserr = 0.0;
  for (auto j=0; j<order; j++) {
    for (auto i=0; i<order; i++) {
      const int ij = i*order+j;
      const int ji = j*order+i;
      const double reference = static_cast<double>(ij)*(1.+iterations)+addit;
      abserr += std::fabs(B[ji] - reference);
    }
  }

#ifdef VERBOSE
  std::cout << "Sum of absolute differences: " << abserr << std::endl;
#endif

  const auto epsilon = 1.0e-8;
  if (abserr < epsilon) {
    std::cout << "Solution validates" << std::endl;
    auto avgtime = trans_time/iterations;
    auto bytes = (size_t)order * (size_t)order * sizeof(double);
    std::cout << "Rate (MB/s): " << 1.0e-6 * (2L*bytes)/avgtime
              << " Avg time (s): " << avgtime << std::endl;
  } else {
    std::cout << "ERROR: Aggregate squared error " << abserr
              << " exceeds threshold " << epsilon << std::endl;
    return 1;
  }

  return 0;
}

