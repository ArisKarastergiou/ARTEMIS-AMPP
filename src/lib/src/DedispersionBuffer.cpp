
#include <QFile>
#include <QTextStream>
#include "DedispersionBuffer.h"
#include <algorithm>
#include "SpectrumDataSet.h"
#include "WeightedSpectrumDataSet.h"
#include <omp.h>

namespace pelican {

namespace lofar {


/**
 *@details DedispersionBuffer 
 */
DedispersionBuffer::DedispersionBuffer( unsigned int size, unsigned int sampleSize,
                                        bool invertChannels )
   : _sampleSize(sampleSize), _invertChannels(invertChannels)
{
    setSampleCapacity(size);
    clear();
}

/**
 *@details
 */
DedispersionBuffer::~DedispersionBuffer()
{
}

void DedispersionBuffer::setSampleCapacity(unsigned int maxSamples)
{
    _nsamp = maxSamples;
    _timedata.resize( maxSamples * _sampleSize );
}

void DedispersionBuffer::dump( const QString& fileName ) const {
    QFile file(fileName);
    if (QFile::exists(fileName)) QFile::remove(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);

    for (int c = 0; c < _timedata.size(); ++c) {
        out << QString::number(_timedata[c], 'g' ) << QString(((c+1)%_nsamp == 0)?"\n":" ");
    }
    file.close();
}

  const QList<SpectrumDataSetStokes*>& DedispersionBuffer::copy( DedispersionBuffer* buf, QVector<float>& noiseTemplate, unsigned int samples )
{
    unsigned int count = 0;
    unsigned int blobIndex = _inputBlobs.size();
    unsigned int sampleNum;
    unsigned int blobSample;
    // copy the memory
    while( count < samples ) {
        Q_ASSERT( blobIndex > 0 );
        SpectrumDataSetStokes* blob = _inputBlobs[--blobIndex];
        //        WeightedSpectrumDataSet* wblob;
        //        wblob->reset(blob);
        unsigned s = blob->nTimeBlocks();
        sampleNum = samples - count; // remaining samples
        if( sampleNum <= s ) {
            // We have all the samples we need in the current blob
            buf->_sampleCount = 0; // offset position to write to
            blobSample = s - sampleNum;
        } else {
            // Take all the samples from this blob, we will need more
            buf->_sampleCount = sampleNum - s;// offset position to write to
            blobSample = 0;
        }
        buf->_addSamples( blob, noiseTemplate, &blobSample, s - blobSample );
        buf->_inputBlobs.push_front( blob );
        count += s;
    }
    buf->_sampleCount = samples;
    return buf->_inputBlobs;
}

  unsigned DedispersionBuffer::addSamples( WeightedSpectrumDataSet* weightedData, QVector<float>& noiseTemplate, unsigned *sampleNumber ) {
    SpectrumDataSetStokes* streamData =
      static_cast<SpectrumDataSetStokes*>(weightedData->dataSet());
    if( ! _inputBlobs.contains(streamData) )
        _inputBlobs.append(streamData);
    unsigned int numSamples = weightedData->dataSet()->nTimeBlocks();
    return _addSamples( weightedData, noiseTemplate, sampleNumber, numSamples );
}

  unsigned DedispersionBuffer::_addSamples( WeightedSpectrumDataSet* weightedData, QVector<float>& noiseTemplate, unsigned *sampleNumber, unsigned numSamples ) {
    Q_ASSERT( streamData != 0 );
    SpectrumDataSetStokes* streamData =
      static_cast<SpectrumDataSetStokes*>(weightedData->dataSet());
    SpectrumDataSet<float>* weights = weightedData->weights();

    unsigned int nChannels = streamData->nChannels();
    unsigned int nSubbands = streamData->nSubbands();
    unsigned int nPolarisations = streamData->nPolarisations();
    nPolarisations = 1; // dedispersion on total power only
    if( nSubbands * nChannels * nPolarisations != _sampleSize ) {
        std::cerr  << "DedispersionBuffer: input data sample size(" <<  nSubbands * nChannels * nPolarisations
                   << ") does not match buffer sample size (" << _sampleSize << ")" << std::endl;
        return spaceRemaining();
    }

    if( _sampleCount == 0 ) {
        // record first sample number
        _firstSample = *sampleNumber;
    }
    unsigned maxSamples = std::min( numSamples, spaceRemaining() + *sampleNumber );
    timerStart(&_addSampleTimer);
    int start = *sampleNumber;
    if( _invertChannels ) {
        int nChannelsMinusOne = nChannels - 1;
        int nSubbandsMinusOne= nSubbands - 1;
        // Try varying x, from 6 down.  Also try it with this commented out.
        //        omp_set_num_threads(6);
        int s, c;
        int localSampleCount = _sampleCount - start; // create a copy for omp to lock
#pragma omp parallel for private(s,c) schedule(dynamic)
        for(int t = start; t < (int)maxSamples; ++t ) {
            for (s = 0; s < (int)nSubbands; ++s ) {
                int bsize = s*nChannels*_nsamp + localSampleCount + t;
                float* data = streamData->spectrumData(t, nSubbandsMinusOne - s, 0);
                const float* weightData = weights->spectrumData(t, nSubbandsMinusOne - s, 0);
                for (c = 0; c < (int)nChannels; ++c ) {
                  //                    _timedata[(bsize + (c * _nsamp))] = data[nChannelsMinusOne - c];

                  // The following equation is used to replace data
                  // samples that have been set to zero by the RFI
                  // clipper to values from a template noise buffer
                  // that obbey the distribution that the RFI clipper
                  // forces
                  data[nChannelsMinusOne - c]  = data[nChannelsMinusOne - c] - (weightData[nChannelsMinusOne - c] - 1) * noiseTemplate[(bsize + (c * _nsamp))]; // change the data
                  _timedata[(bsize + (c * _nsamp))] = data[nChannelsMinusOne - c];
                }
            }
        }
    } else {
        int s, c;
        int localSampleCount = _sampleCount - start; // create a copy for omp to lock
#pragma omp parallel for private(s,c) schedule(dynamic)
        for(int t = start; t < (int)maxSamples; ++t) {
            int sampleOffset = localSampleCount + t;
            for ( s = 0; s < (int)nSubbands; ++s) {
                float* data = streamData->spectrumData(t, s, 0);
                const float* weightData = weights->spectrumData(t, s, 0);
                int bsize = s*nChannels * _nsamp + sampleOffset;
                for ( c = 0; c < (int)nChannels; ++c) {
                  //                    _timedata[ bsize + ( c * _nsamp ) ] = data[c];
                  data[c] = data[c] - (weightData[c] - 1) * noiseTemplate[bsize + (c * _nsamp)];
                  _timedata[ bsize + (c * _nsamp)] = data[c];
                    }
            }
        }
    }
    _sampleCount += (maxSamples - start);
    *sampleNumber = maxSamples;
    timerUpdate(&_addSampleTimer);
    timerReport(&_addSampleTimer, "DedispersionBuffer::addSamples");
    return spaceRemaining();
}

  unsigned DedispersionBuffer::_addSamples( SpectrumDataSetStokes* streamData, QVector<float>& noiseTemplate, unsigned *sampleNumber, unsigned numSamples ) {
    Q_ASSERT( streamData != 0 );
    unsigned int nChannels = streamData->nChannels();
    unsigned int nSubbands = streamData->nSubbands();
    unsigned int nPolarisations = streamData->nPolarisations();
    nPolarisations = 1; // dedispersion on total power only
    if( nSubbands * nChannels * nPolarisations != _sampleSize ) {
        std::cerr  << "DedispersionBuffer: input data sample size(" <<  nSubbands * nChannels * nPolarisations
                   << ") does not match buffer sample size (" << _sampleSize << ")" << std::endl;
        return spaceRemaining();
    }

    if( _sampleCount == 0 ) {
        // record first sample number
        _firstSample = *sampleNumber;
    }
    unsigned maxSamples = std::min( numSamples, spaceRemaining() + *sampleNumber );
    timerStart(&_addSampleTimer);
    int start = *sampleNumber;
    if( _invertChannels ) {
        int nChannelsMinusOne = nChannels - 1;
        int nSubbandsMinusOne= nSubbands - 1;
        // Try varying x, from 6 down.  Also try it with this commented out.
        //        omp_set_num_threads(6);
        int s, c;
        int localSampleCount = _sampleCount - start; // create a copy for omp to lock
#pragma omp parallel for private(s,c) schedule(dynamic)
        for(int t = start; t < (int)maxSamples; ++t ) {
            for (s = 0; s < (int)nSubbands; ++s ) {
                int bsize = s*nChannels*_nsamp + localSampleCount + t;
                float* data = streamData->spectrumData(t, nSubbandsMinusOne - s, 0);
                for (c = 0; c < (int)nChannels; ++c ) {
                  //                    _timedata[(bsize + (c * _nsamp))] = data[nChannelsMinusOne - c];

                  // The following equation is used to replace data
                  // samples that have been set to zero by the RFI
                  // clipper to values from a template noise buffer
                  // that obbey the distribution that the RFI clipper
                  // forces
                  _timedata[(bsize + (c * _nsamp))] = data[nChannelsMinusOne - c];
                }
            }
        }
    } else {
        int s, c;
        int localSampleCount = _sampleCount - start; // create a copy for omp to lock
#pragma omp parallel for private(s,c) schedule(dynamic)
        for(int t = start; t < (int)maxSamples; ++t) {
            int sampleOffset = localSampleCount + t;
            for ( s = 0; s < (int)nSubbands; ++s) {
                float* data = streamData->spectrumData(t, s, 0);
                int bsize = s*nChannels * _nsamp + sampleOffset;
                for ( c = 0; c < (int)nChannels; ++c) {
                  _timedata[ bsize + (c * _nsamp)] = data[c];
                    }
            }
        }
    }
    _sampleCount += (maxSamples - start);
    *sampleNumber = maxSamples;
    timerUpdate(&_addSampleTimer);
    timerReport(&_addSampleTimer, "DedispersionBuffer::addSamples");
    return spaceRemaining();
}

void DedispersionBuffer::clear() {
    _sampleCount = 0;
    _inputBlobs.clear();
}

} // namespace lofar
} // namespace pelican
