#include "SigProcPipeline.h"
#include "WeightedSpectrumDataSet.h"


namespace pelican {

namespace lofar {


/**
 *@details SigProcPipeline 
 */
SigProcPipeline::SigProcPipeline()
    : AbstractPipeline()
{
}

/**
 *@details
 */
SigProcPipeline::~SigProcPipeline()
{
}

void SigProcPipeline::init() {
    rfiClipper = (RFI_Clipper *) createModule("RFI_Clipper");
    stokesIntegrator = (StokesIntegrator *) createModule("StokesIntegrator");
    intStokes = (SpectrumDataSetStokes*) createBlob("SpectrumDataSetStokes");
    //weightedIntStokes = (WeightedSpectrumDataSet*) createBlob("WeightedSpectrumDataSet");

    // Request remote data
    requestRemoteData("SpectrumDataSetStokes");
}

void SigProcPipeline::run(QHash<QString, DataBlob*>& remoteData)
{
    SpectrumDataSetStokes* stokes = (SpectrumDataSetStokes*)remoteData["SigProcData"];
    if( ! stokes ) throw(QString("no STOKES!"));
    weightedIntStokes->reset(stokes);
    rfiClipper->run(weightedIntStokes);

    stokesIntegrator->run(stokes, intStokes);
    dataOutput(intStokes, "SpectrumDataSetStokes");
}

} // namespace lofar
} // namespace pelican
