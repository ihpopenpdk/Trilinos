// @HEADER
//
// ***********************************************************************
//
//        MueLu: A package for multigrid based preconditioning
//                  Copyright 2012 Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact
//                    Jeremie Gaidamour (jngaida@sandia.gov)
//                    Jonathan Hu       (jhu@sandia.gov)
//                    Ray Tuminaro      (rstumin@sandia.gov)
//
// ***********************************************************************
//
// @HEADER
/*
 * MueLu_SegregationATransferFactory.hpp
 *
 *  Created on: Oct 27, 2011
 *      Author: wiesner
 */

#ifndef MUELU_SEGREGATIONATRANSFERFACTORY_HPP
#define MUELU_SEGREGATIONATRANSFERFACTORY_HPP

#include "MueLu_ConfigDefs.hpp"

#include "MueLu_TwoLevelFactoryBase.hpp"
#include "MueLu_Level.hpp"
#include "MueLu_Exceptions.hpp"
//#include "MueLu_Utilities.hpp"

namespace MueLu {
/*!
  @class SegregationATransferFactory class.
  @brief special transfer factory for SegregationAFilterFactory
*/
  template <class Scalar = double, class LocalOrdinal = int, class GlobalOrdinal = LocalOrdinal, class Node = Kokkos::DefaultNode::DefaultNodeType, class LocalMatOps = typename Kokkos::DefaultKernels<void, LocalOrdinal, Node>::SparseOps>
  class SegregationATransferFactory : public TwoLevelFactoryBase {
#undef MUELU_SEGREGATIONATRANSFERFACTORY_SHORT
#include "MueLu_UseShortNames.hpp"

    typedef Xpetra::MapExtractor<Scalar, LocalOrdinal, GlobalOrdinal, Node> MapExtractorClass;
    typedef Xpetra::MapExtractorFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node> MapExtractorFactoryClass;

  public:
    //@{ Constructors/Destructors.
    SegregationATransferFactory(RCP<FactoryBase> PtentFact = Teuchos::null)
    : PtentFact_(PtentFact) {}

    virtual ~SegregationATransferFactory() {}
    //@}

    //! Input
    //@{

    void DeclareInput(Level &fineLevel, Level &coarseLevel) const {
      coarseLevel.DeclareInput("P",PtentFact_.get(),this);  // tentative prolongator, needed for finding corresponding coarse level gids
      fineLevel.DeclareInput("SegAMapExtractor", MueLu::NoFactory::get(),this);
    }

    //@}

    //@{ Build methods.
    void Build(Level &fineLevel, Level &coarseLevel) const {

      TEUCHOS_TEST_FOR_EXCEPTION(!fineLevel.IsAvailable("SegAMapExtractor",MueLu::NoFactory::get()), Exceptions::RuntimeError, "MueLu::SegregationATransferFactory::Build(): SegAMapExtractor variable not available. Check if it has been generated and set in MueLu_SegregationAFilterFactory!");
      TEUCHOS_TEST_FOR_EXCEPTION(!coarseLevel.IsAvailable("P",PtentFact_.get()), Exceptions::RuntimeError, "MueLu::SegregationATransferFactory::Build(): P (generated by TentativePFactory not available.");

      // get map extractor and tentative variables from level classes
      RCP<const MapExtractorClass> fineMapExtractor = fineLevel.Get< RCP<const MapExtractorClass> >("SegAMapExtractor",MueLu::NoFactory::get());
      RCP<Operator> Ptent = coarseLevel.Get< RCP<Operator> >("P", PtentFact_.get());

      // DEBUG //
      //Teuchos::RCP<Teuchos::FancyOStream> out = Teuchos::getFancyOStream(Teuchos::rcpFromRef(std::cout));
      //Ptent->describe(*out,Teuchos::VERB_EXTREME);
      // DEBUG //

      // prepare variables
      size_t numMaps = fineMapExtractor->NumMaps();
      std::vector<std::vector<GlobalOrdinal> > coarseGids(numMaps);

      // loop over local rows of Ptent
      for(size_t row=0; row<Ptent->getNodeNumRows(); row++)
      {
        // get global row id
        GlobalOrdinal grid = Ptent->getRowMap()->getGlobalElement(row); // global row id

        // check in which submap of mapextractor grid belongs to
        LocalOrdinal blockId = -Teuchos::ScalarTraits<LocalOrdinal>::one();
        for (size_t bb=0; bb<fineMapExtractor->NumMaps(); bb++) {
          const RCP<const Map> cmap = fineMapExtractor->getMap(bb);
          if (cmap->isNodeGlobalElement(grid)) {
            blockId = bb;
            break;
          }
        }

        // if no information is to be transfered to the next coarser level -> proceed with next row
        //if(blockId == -Teuchos::ScalarTraits<LocalOrdinal>::one()) continue;
        TEUCHOS_TEST_FOR_EXCEPTION(blockId == -1, Exceptions::RuntimeError, "MueLu::SegregationATransferFactory::Build: BlockId == -1. Error.");

        // extract data from Ptent
        size_t nnz = Ptent->getNumEntriesInLocalRow(row);
        TEUCHOS_TEST_FOR_EXCEPTION(nnz==0, Exceptions::RuntimeError, "MueLu::SegregationATransferFactory::Build: zero row in Ptent!. Error.");
        Teuchos::ArrayView<const LocalOrdinal> indices;
        Teuchos::ArrayView<const Scalar> vals;
        Ptent->getLocalRowView(row, indices, vals);
        TEUCHOS_TEST_FOR_EXCEPTION(Teuchos::as<size_t>(indices.size()) != nnz, Exceptions::RuntimeError, "MueLu::SegregationATransferFactory::Build: number of nonzeros not equal to number of indices? Error.");

        for (size_t i=0; i<nnz; i++) {
          // get global column id
          GlobalOrdinal gcid = Ptent->getColMap()->getGlobalElement(indices[i]); // LID -> GID (column)

          if(Ptent->getColMap()->isNodeLocalElement(indices[i])==true) {
            coarseGids[blockId].push_back(gcid);
          }
        }
      }

      // build column maps
      std::vector<RCP<const Map> > colMaps(numMaps);
      for(size_t i=0; i<numMaps; i++) {
        std::vector<GlobalOrdinal> gidvector = coarseGids[i];
        std::sort(gidvector.begin(), gidvector.end());
        gidvector.erase(std::unique(gidvector.begin(), gidvector.end()), gidvector.end());
        //TEUCHOS_TEST_FOR_EXCEPTION(gidvector.size()==0, Exceptions::RuntimeError, "MueLu::SegregationATransferFactory::Build: empty column map. Error.");

        Teuchos::ArrayView<GlobalOrdinal> gidsForMap(&gidvector[0],gidvector.size());
        colMaps[i] = Xpetra::MapFactory<LocalOrdinal, GlobalOrdinal, Node>::Build(Ptent->getColMap()->lib(), gidsForMap.size(), gidsForMap, Ptent->getColMap()->getIndexBase(), Ptent->getColMap()->getComm());
        TEUCHOS_TEST_FOR_EXCEPTION(colMaps[i]==Teuchos::null, Exceptions::RuntimeError, "MueLu::SegregationATransferFactory::Build: error when building column map.");
        //TEUCHOS_TEST_FOR_EXCEPTION((colMaps[i])->getGlobalNumElements()==0, Exceptions::RuntimeError, "MueLu::SegregationATransferFactory::Build: empty column map. Cannot be since gidvectors were not empty.");
        TEUCHOS_TEST_FOR_EXCEPTION((colMaps[i])->getGlobalNumElements()!=gidvector.size(), Exceptions::RuntimeError, "MueLu::SegregationATransferFactory::Build: size of map does not fit to size of gids.");
      }

      // build coarse level MapExtractor
      Teuchos::RCP<const MapExtractorClass> coarseMapExtractor = Xpetra::MapExtractorFactory<Scalar,LocalOrdinal,GlobalOrdinal,Node>::Build(Ptent->getDomainMap(),colMaps);

      // store map extractor in coarse level
      coarseLevel.Set("SegAMapExtractor", coarseMapExtractor, MueLu::NoFactory::get());

    }
    //@}

private:
  //! tentative P Factory (used to split maps)
  RCP<FactoryBase> PtentFact_;

}; //class SegregationATransferFactory

} //namespace MueLu

#define MUELU_SEGREGATIONATRANSFERFACTORY_SHORT

#endif // MUELU_SEGREGATIONATRANSFERFACTORY_HPP