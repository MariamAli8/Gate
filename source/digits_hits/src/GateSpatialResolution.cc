

/*----------------------
  Copyright (C): OpenGATE Collaboration

  This software is distributed under the terms
  of the GNU Lesser General  Public Licence (LGPL)
  See LICENSE.md for further details
  ----------------------*/

/*!
  \class  GateSpatialResolution
  \brief  Digitizer Module for simulating a Gaussian blurring on the position.

  Includes functionalities from:
  	  GateSpblurring
	  GateCC3DlocalSpblurring
	  GateDoIModels (TODO)

  Previous authors: Steven.Staelens@rug.ac.be(?), AE

  - modified by Adrien Paillet 11/2022
  	  This blurring has been validated up to a given FWHM of 10mm.
  	  At higher FWHM, the number of "relocated" digis is no longer negligible. The blurring effect is then so compensated that resolution will improve compared to lower values of FWHM.
-modified by Radia Oudihat 06/2024
       -Added support for 1D and 2D FWHM distributions for X and Y,and applied Gaussian blurring.
*/

#include "GateSpatialResolution.hh"
#include "GateSpatialResolutionMessenger.hh"
#include "GateDigi.hh"

#include "GateDigitizerMgr.hh"
#include "GateObjectStore.hh"
#include "GateConstants.hh"

#include "G4SystemOfUnits.hh"
#include "G4EventManager.hh"
#include "G4Event.hh"
#include "G4SDManager.hh"
#include "G4DigiManager.hh"
#include "G4ios.hh"
#include "G4UnitsTable.hh"
#include "G4TransportationManager.hh"
#include "G4Navigator.hh"
#include "GateVDistribution.hh"




GateSpatialResolution::GateSpatialResolution(GateSinglesDigitizer *digitizer, G4String name)
  :GateVDigitizerModule(name,"digitizerMgr/"+digitizer->GetSD()->GetName()+"/SinglesDigitizer/"+digitizer->m_digitizerName+"/"+name,digitizer,digitizer->GetSD()),
   m_fwhm(0),
   m_fwhmX(0),
   m_fwhmXdistrib(0),
   m_fwhmYdistrib(0),
   m_fwhmXdistrib2D(0),
    m_fwhmYdistrib2D(0),
   m_fwhmY(0),
   m_fwhmZ(0),
   m_IsConfined(true),
   m_Navigator(0),
   m_Touchable(0),
   m_systemDepth(-1),
   m_outputDigi(0),
   m_OutputDigiCollection(0),
   m_digitizer(digitizer)
 {
	G4String colName = digitizer->GetOutputName() ;
	collectionName.push_back(colName);
	m_Messenger = new GateSpatialResolutionMessenger(this);
}


GateSpatialResolution::~GateSpatialResolution()
{
  delete m_Messenger;

}


void GateSpatialResolution::Digitize()
{
	if( m_fwhm!=0 && (m_fwhmX!=0 || m_fwhmY!=0 || m_fwhmZ!=0  ))
		{
			GateError("***ERROR*** Spatial Resolution is ambiguous: you can set unique /fwhm for 3 axis OR set /fhwmX, /fhwmY, /fhwmZ!");
		}

	G4double fwhmX;
	G4double fwhmY;
	G4double fwhmZ;


	if (m_fwhmX==0 && m_fwhmY==0 && m_fwhmZ==0 )
	{
		fwhmX=m_fwhm;
		fwhmY=m_fwhm;
		fwhmZ=m_fwhm;

	}
	else
	{
		fwhmX=m_fwhmX;
		fwhmY=m_fwhmY;
		fwhmZ=m_fwhmZ;



	}


	GateVSystem* m_system =  ((GateSinglesDigitizer*)this->GetDigitizer())->GetSystem();

	if (m_system==NULL) G4Exception( "GateSpatialResolution::Digitize", "Digitize", FatalException,
				 "Failed to get the system corresponding to that digitizer. Abort.\n");

	if (!m_system->CheckIfEnoughLevelsAreDefined())
	{
		 GateError( " *** ERROR*** GateSpatialResolution::Digitize. Not all defined geometry levels has their mother levels defined."
				 "(Ex.: for cylindricalPET, the levels are: rsector, module, submodule, crystal). If you have defined submodule, you have to have resector and module defined as well."
				 "Please, add them to your geometry macro in /gate/systems/cylindricalPET/XXX/attach    YYY. Abort.\n");
	}

	m_systemDepth = m_system->GetTreeDepth();


	G4String digitizerName = m_digitizer->m_digitizerName;
	G4String outputCollName = m_digitizer-> GetOutputName();

	m_OutputDigiCollection = new GateDigiCollection(GetName(),outputCollName); // to create the Digi Collection

	G4DigiManager* DigiMan = G4DigiManager::GetDMpointer();

	GateDigiCollection* IDC = 0;
	IDC = (GateDigiCollection*) (DigiMan->GetDigiCollection(m_DCID));

	GateDigi* inputDigi;


  if (IDC)
     {
	  G4int n_digi = IDC->entries();

	  //loop over input digits
	  for (G4int i=0;i<n_digi;i++)
	  {
		  inputDigi=(*IDC)[i];
		  m_outputDigi = new GateDigi(*inputDigi);


		  G4ThreeVector P = inputDigi->GetVolumeID().MoveToBottomVolumeFrame(inputDigi->GetGlobalPos()); //TC

		  G4double Px = P.x();
		  G4double Py = P.y();
		  G4double Pz = P.z();
		  G4double stddevX, stddevY, stddevZ;

		  if (m_fwhmXdistrib) {
		      // If the FWHM distribution for X is defined
		      if (m_fwhmYdistrib) {
		          // If the FWHM distribution for Y is also defined
		          stddevX = m_fwhmXdistrib->Value(P.x() * mm);
		          stddevY = m_fwhmYdistrib->Value(P.y() * mm);
		      } else if (m_fwhmY) {
		          // If the constant FWHM for Y is defined
		          stddevX = m_fwhmXdistrib->Value(P.x() * mm);
		          stddevY = fwhmY / GateConstants::fwhm_to_sigma;
		      }     // If the 2D distribution for Y is defined
		      else if (m_fwhmYdistrib2D) {
		          // Use the constant value for X and the 2D distribution for Y
		          stddevX = fwhmX / GateConstants::fwhm_to_sigma;
		          stddevY = m_fwhmYdistrib2D->Value2D(P.x() * mm, P.y() * mm);
		      }
		      // If only the distribution for X is defined
		      else {
		          // Use the distribution for X and the constant value for Y
		          stddevX = m_fwhmXdistrib->Value(P.x() * mm);
		          stddevY = fwhmY / GateConstants::fwhm_to_sigma;
		      }

		  } else if (m_fwhmXdistrib2D) {
		      // If the 2D FWHM distribution for X is defined
		      if (m_fwhmYdistrib) {
		          // If the FWHM distribution for Y is defined
		          stddevX = m_fwhmXdistrib2D->Value2D(P.x() * mm, P.y() * mm);
		          stddevY = m_fwhmYdistrib->Value(P.y() * mm);
		      } else if (m_fwhmY) {
		          // If the constant FWHM for Y is defined
		          stddevX = m_fwhmXdistrib2D->Value2D(P.x() * mm, P.y() * mm);
		          stddevY = fwhmY / GateConstants::fwhm_to_sigma;
		      } else {
		          // If neither the FWHM distribution nor constant for Y is defined
		          stddevX = stddevY = m_fwhmXdistrib2D->Value2D(P.x() * mm, P.y() * mm);
		      }
		  } else if (m_fwhmYdistrib) {
		      // If the FWHM distribution for Y is defined
		      if (m_fwhmXdistrib) {
		          // If the FWHM distribution for X is also defined
		          stddevX = m_fwhmXdistrib->Value(P.x() * mm);
		          stddevY = m_fwhmYdistrib->Value(P.y() * mm);
		      } else if (m_fwhmX) {
		          // If the constant FWHM for X is defined
		          stddevX = fwhmX / GateConstants::fwhm_to_sigma;
		          stddevY = m_fwhmYdistrib->Value(P.y() * mm);
		      } else {
		          // If the 2D distribution for X is defined
		          stddevX = m_fwhmXdistrib2D->Value2D(P.x() * mm, P.y() * mm);
		          stddevY = m_fwhmYdistrib->Value(P.y() * mm);
		      }
		  } else {
		      // If neither the FWHM distributions for X nor Y are defined
		      stddevX = fwhmX / GateConstants::fwhm_to_sigma;
		      stddevY = fwhmY / GateConstants::fwhm_to_sigma;
		  }

		  G4double PxNew = G4RandGauss::shoot(Px,stddevX);
		  G4double PyNew = G4RandGauss::shoot(Py,stddevY);
		  G4double PzNew = G4RandGauss::shoot(Pz,fwhmZ/GateConstants::fwhm_to_sigma);
		  if (m_IsConfined)
		  {
			  //set the position on the border of the crystal
			  //no need to update volume ID
			inputDigi->GetVolumeID().GetBottomCreator()->GetLogicalVolume()->GetSolid()->CalculateExtent(kXAxis, limits, at, Xmin, Xmax);
			inputDigi->GetVolumeID().GetBottomCreator()->GetLogicalVolume()->GetSolid()->CalculateExtent(kYAxis, limits, at, Ymin, Ymax);
			inputDigi->GetVolumeID().GetBottomCreator()->GetLogicalVolume()->GetSolid()->CalculateExtent(kZAxis, limits, at, Zmin, Zmax);

			if(PxNew<Xmin) PxNew=Xmin;
			if(PyNew<Ymin) PyNew=Ymin;
			if(PzNew<Zmin) PzNew=Zmin;
			if(PxNew>Xmax) PxNew=Xmax;
			if(PyNew>Ymax) PyNew=Ymax;
			if(PzNew>Zmax) PzNew=Zmax;



			m_outputDigi->SetLocalPos(G4ThreeVector(PxNew,PyNew,PzNew)); //TC
			m_outputDigi->SetGlobalPos(m_outputDigi->GetVolumeID().MoveToAncestorVolumeFrame(m_outputDigi->GetLocalPos())); //TC
			//TC
			//outputPulse->SetGlobalPos(G4ThreeVector(PxNew,PyNew,PzNew));
			m_OutputDigiCollection->insert(m_outputDigi);
			//G4cout<<"PxNew"<<PxNew<<Gateendl;
		  }
		  else
		  {
			//Not confined:
			//Update volume IDs and new locations inside crystal

			// TODO Test properly and maybe extent to more general cases
			  inputDigi->GetVolumeID().GetCreator(m_systemDepth-1)->GetLogicalVolume()->GetSolid()->CalculateExtent(kXAxis, limits, at, Xmin, Xmax);
			  inputDigi->GetVolumeID().GetCreator(m_systemDepth-1)->GetLogicalVolume()->GetSolid()->CalculateExtent(kYAxis, limits, at, Ymin, Ymax);
			  inputDigi->GetVolumeID().GetCreator(m_systemDepth-1)->GetLogicalVolume()->GetSolid()->CalculateExtent(kZAxis, limits, at, Zmin, Zmax);

			  if(PxNew<Xmin) PxNew=Xmin;
			  if(PyNew<Ymin) PyNew=Ymin;
			  if(PzNew<Zmin) PzNew=Zmin;
			  if(PxNew>Xmax) PxNew=Xmax;
			  if(PyNew>Ymax) PyNew=Ymax;
			  if(PzNew>Zmax) PzNew=Zmax;
			   m_outputDigi->SetLocalPos(G4ThreeVector(PxNew,PyNew,PzNew)); //TC
			  m_outputDigi->SetGlobalPos(m_outputDigi->GetVolumeID().MoveToAncestorVolumeFrame(m_outputDigi->GetLocalPos())); //TC

			 //Getting world Volume
			  // Do not use from TransportationManager as it is not recommended
			   G4Navigator *navigator = G4TransportationManager::GetTransportationManager()->GetNavigatorForTracking();
			  G4VPhysicalVolume *WorldVolume = navigator->GetWorldVolume();
			  m_Navigator = new G4Navigator();
			  m_Navigator->SetWorldVolume(WorldVolume);

			  G4VPhysicalVolume* PV = m_Navigator->LocateGlobalPointAndSetup(m_outputDigi->GetGlobalPos());
			  m_Touchable = m_Navigator->CreateTouchableHistoryHandle();
			  G4int hdepth = m_Touchable->GetHistoryDepth(); // zero always!

			  if ( hdepth == m_systemDepth  )
			  {
				  UpdateVolumeID();
			  }
			  m_OutputDigiCollection->insert(m_outputDigi);



		  }

	  }
	  }
  else
    {
  	  if (nVerboseLevel>1)
  	  	G4cout << "[GateSpatialResolution::Digitize]: input digi collection is null -> nothing to do\n\n";
  	    return;
    }
  StoreDigiCollection(m_OutputDigiCollection);

}





void GateSpatialResolution::UpdateVolumeID()
{
 for (G4int i=1;i<m_systemDepth;i++)
		{
		G4int CopyNo = m_Touchable->GetReplicaNumber(m_systemDepth-1-i);
		m_outputDigi->ChangeVolumeIDAndOutputVolumeIDValue(i,CopyNo);
		}


}

void GateSpatialResolution::DescribeMyself(size_t indent )
{
	if(m_fwhm)
	  G4cout << GateTools::Indent(indent) << "Spatial resolution : " << m_fwhm  << Gateendl;
	else
		G4cout << GateTools::Indent(indent) << "Spatial resolution : " << m_fwhmX <<" "<< m_fwhmY<< " "<<m_fwhmZ<< Gateendl;}
