/****************************************************************************
 *
 * This file is part of the ustk software.
 * Copyright (C) 2016 - 2017 by Inria. All rights reserved.
 *
 * This software is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * ("GPL") version 2 as published by the Free Software Foundation.
 * See the file LICENSE.txt at the root directory of this source
 * distribution for additional information about the GNU GPL.
 *
 * For using ustk with software that can not be combined with the GNU
 * GPL, please contact Inria about acquiring a ViSP Professional
 * Edition License.
 *
 * This software was developed at:
 * Inria Rennes - Bretagne Atlantique
 * Campus Universitaire de Beaulieu
 * 35042 Rennes Cedex
 * France
 *
 * If you have questions regarding the use of this file, please contact
 * Inria at ustk@inria.fr
 *
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors:
 * Pedro Patlan
 * Marc Pouliquen
 *
 *****************************************************************************/

/**
 * @file usRFToPreScan2DConverter.cpp
 * @brief 2D scan-converter
 */

#include <visp3/ustk_core/usRFToPreScan2DConverter.h>

#if defined(USTK_HAVE_FFTW)

/**
* Constructor.
* @param decimationFactor Decimation factor : keep only 1 pre-scan sample every N sample (N = decimationFactor)
*/
usRFToPreScan2DConverter::usRFToPreScan2DConverter(int widthRF, int heightRF, int decimationFactor) : m_logCompressor(),
  m_decimationFactor(decimationFactor) {

  init(widthRF, heightRF);
}

/**
* Destructor.
*/
usRFToPreScan2DConverter::~usRFToPreScan2DConverter() {
  fftw_free(m_fft_in); fftw_free(m_fft_out); fftw_free(m_fft_conv); fftw_free(m_fft_out_inv);
}


/**
* Init method, to pre-allocate memory for all the processes (fft inputs/outputs, log compression output).
*/
void usRFToPreScan2DConverter::init(int widthRF, int heigthRF) {

  // for FFT
  m_fft_in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * heigthRF);
  m_fft_out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * heigthRF);
  m_fft_conv = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * heigthRF);
  m_fft_out_inv = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * heigthRF);

  // for Hilbert transform
  m_sa = (std::complex<double> *) malloc(heigthRF * sizeof(std::complex<double> ));

  m_signalSize = heigthRF;
  m_scanLineNumber = widthRF;
}

/*!
 * \brief SProcessing::m_Hilbert
 * This function computes the Hilbert transform of a signal s
 * \param s: Signal of 16-bit values
 * \return std::vector<std::complex<double> > that contains the Hilbert transform
 */
std::complex<double> * usRFToPreScan2DConverter::HilbertTransform(const short int *s)
{
  ///time of Hilbert transform
  //const clock_t begin_time = clock();
  fftw_plan p, pinv;
  int N = m_signalSize;


  p = fftw_plan_dft_1d(N, m_fft_in, m_fft_out, FFTW_FORWARD, FFTW_ESTIMATE);
  // Put signal data s into in
  for(int i = 0; i < N; i++)
  {
    m_fft_in[i][0] = (double)s[i];
    m_fft_in[i][1] = 0.0;
  }

  // Obtain the FFT
  fftw_execute(p);

  for(int i= 0; i < N; i++)
  {

    if(i<N/2)
      m_fft_out[i][1]=-1*m_fft_out[i][1];
    if(i==N/2)
    {
      m_fft_out[i][0]=0;
      m_fft_out[i][1]=0;
    }
    if(i>N/2)
      m_fft_out[i][0]=-1*m_fft_out[i][0];
    if(i==0)
    {
      m_fft_out[i][0]=0;
      m_fft_out[i][1]=0;
    }

    double z=m_fft_out[i][1];
    m_fft_out[i][1]=m_fft_out[i][0];
    m_fft_out[i][0]=z;
  }

  // FFT Inverse
  pinv = fftw_plan_dft_1d(N, m_fft_out, m_fft_out_inv, FFTW_BACKWARD, FFTW_ESTIMATE);
  // Obtain the IFFT
  fftw_execute(pinv);
  fftw_destroy_plan(p); fftw_destroy_plan(pinv);
  // Put the iFFT output in sa
  for(int i = 0; i < N; i++)
  {
    m_sa[i] = std::complex<double> (m_fft_in[i][0], -m_fft_out_inv[i][0]/(double)N);
  }
  return m_sa;
}

void usRFToPreScan2DConverter::sqrtAbsv(std::complex<double> * cv, double* out)
{
  for(int i = 0; i < m_signalSize; i++)
  {
    out[i] = (unsigned char) sqrt(abs(cv[i]));
  }
}

/**
* Convert method : performs the conversion from RF frame to a pre-scan frame using the following processes :
* - Enveloppe detector
* - Logarithmic compression
* - Decimation
*
* @param rfImage RF frame to convert
* @param preScanImage pre-scan image : result of convertion
*/
void usRFToPreScan2DConverter::convert(const usImageRF2D<short int> &rfImage, usImagePreScan2D<unsigned char> &preScanImage) {

  preScanImage.resize(rfImage.getHeight() / m_decimationFactor,rfImage.getWidth());

  // First we copy the transducer settings
  preScanImage.setImagePreScanSettings(rfImage);

  int w = rfImage.getWidth();
  int h = rfImage.getHeight();

  unsigned int frameSize = w*h;
  double *env = new double[frameSize];

  unsigned char *comp = new unsigned char[frameSize];

  // Run envelope detector
  for (int i = 0; i < w; ++i) {
    sqrtAbsv(HilbertTransform(rfImage.bitmap + i*h), env + i * h);
  }

  // Log-compress
  m_logCompressor.run(comp, env, frameSize);

  //find min & max values
  double min = 1e8;
  double max = -1e8;
  for (unsigned int i = 0; i < frameSize; ++i) {
    if (comp[i] < min)
      min = comp[i];
    if (comp[i] > max)
      max = comp[i];
  }

  //max-min computation
  double maxMinDiff = max - min;

  //Decimate and normalize
  int k = 0;
  for (int i = 0; i < h; i+=m_decimationFactor) {
    for (int j = 0; j < w ; ++j) {
      unsigned int  vcol = (unsigned int) (((comp[i + h * j] - min) / maxMinDiff) * 255);
      preScanImage[k][j] = (vcol>255)?255:vcol;
    }
    k++;
  }
}

/**
* Decimation factor getter.
* @return Decimation factor : keep only 1 pre-scan sample every N sample (N = decimationFactor)
*/
int usRFToPreScan2DConverter::getDecimationFactor() {
  return m_decimationFactor;
}

/**
* Decimation factor setter.
* @param  decimationFactor : keep only 1 pre-scan sample every N sample (N = decimationFactor)
*/
void usRFToPreScan2DConverter::setDecimationFactor(int decimationFactor) {
  m_decimationFactor = decimationFactor;
}

#endif
