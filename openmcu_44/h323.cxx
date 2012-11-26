
#include <ptlib.h>
#include <ptlib/video.h>

#include "version.h"
#include "mcu.h"
#include "h323.h"

#include <h323pdu.h>
#include <math.h>
#include <stdio.h>

#if OPENMCU_VIDEO

#if USE_LIBYUV
#include <libyuv/scale.h>
#endif

const unsigned int DefaultVideoFrameRate = 10;
const unsigned int DefaultVideoQuality   = 10;
static const char EnableVideoKey[]       = "Enable video";
static const char VideoFrameRateKey[]    = "Video frame rate";
static const char VideoQualityKey[]      = "Video quality";

#endif  // OPENMCU_VIDEO

static const char InterfaceKey[]          = "Interface";
static const char LocalUserNameKey[]      = "Local User Name";
static const char GatekeeperUserNameKey[] = "Gatekeeper Username";
static const char GatekeeperAliasKey[]    = "Gatekeeper Room Names";
static const char GatekeeperPasswordKey[] = "Gatekeeper Password";
static const char GatekeeperPrefixesKey[] = "Gatekeeper Prefixes";
static const char GatekeeperModeKey[]     = "Gatekeeper Mode";
static const char GatekeeperKey[]         = "Gatekeeper";
static const char DisableCodecsKey[]      = "Disable codecs";
static const char NATRouterIPKey[]        = "NAT Router IP";

static const char * GKModeLabels[] = { 
   "No gatekeeper", 
   "Find gatekeeper", 
   "Use gatekeeper", 
};

enum {
  Gatekeeper_None,
  Gatekeeper_Find,
  Gatekeeper_Explicit
};

#define GKMODE_LABEL_COUNT   (sizeof(GKModeLabels)/sizeof(char *))

#define new PNEW


///////////////////////////////////////////////////////////////

void SpliceMacro(PString & text, const PString & token, const PString & value)
{
  PRegularExpression RegEx("<?!--#status[ \t\r\n]+" + token + "[ \t\r\n]*-->?",
                           PRegularExpression::Extended|PRegularExpression::IgnoreCase);
  PINDEX pos, len;
  while (text.FindRegEx(RegEx, pos, len))
    text.Splice(value, pos, len);
}

///////////////////////////////////////////////////////////////

OpenMCUH323EndPoint::OpenMCUH323EndPoint(ConferenceManager & _conferenceManager)
  : conferenceManager(_conferenceManager)
{
#if OPENMCU_VIDEO
	terminalType = e_MCUWithAVMP;
  enableVideo  = TRUE;
  videoRate    = DefaultVideoFrameRate;
#else
	terminalType = e_MCUWithAudioMP;
#endif

   gkAlias = PString();

}

void OpenMCUH323EndPoint::Initialise(PConfig & cfg, PConfigPage * rsrc)
{

#ifdef HAS_AEC
  SetAECEnabled(FALSE);
#endif

///////////////////////////////////////////
// Listeners
  PString defaultInterface = "*:1720";
  H323TransportAddressArray interfaces;
  PINDEX arraySize = cfg.GetInteger(PString(InterfaceKey) + " Array Size");
  if (arraySize == 0)
    StartListener(defaultInterface);
  else {
    for (int i = 0; i < arraySize; i++)
      interfaces.Append(new H323TransportAddress(cfg.GetString(psprintf("%s %u", InterfaceKey, i+1), "")));
    StartListeners(interfaces);
  }
  rsrc->Add(new PHTTPFieldArray(new PHTTPStringField(cfg.GetDefaultSection() + "\\" + InterfaceKey, InterfaceKey, 20, defaultInterface), FALSE));

  if (listeners.IsEmpty()) {
    PSYSTEMLOG(Fatal, "Main\tCould not open H.323 Listener");
  }

  AliasList.RemoveAll();
  localAliasNames.RemoveAll();

///////////////////////////////////////////
// NAT Router IP
  PString nat_ip = cfg.GetString(NATRouterIPKey);
  rsrc->Add(new PHTTPStringField(NATRouterIPKey, 25, nat_ip,"Type global IP address or leave blank if OpenMCU isn't behind NAT"));
  if (nat_ip.Trim().IsEmpty()) {
    behind_masq = FALSE;
  } else {
    masqAddressPtr = new PIPSocket::Address(nat_ip);
    behind_masq = TRUE;
    cout << "Masquerading as address " << *(masqAddressPtr) << endl;
  }

//////////////////////////////////////////////////////
// Gatekeeper mode
  PStringArray labels(GKMODE_LABEL_COUNT, GKModeLabels); 
  PINDEX idx = labels.GetStringsIndex(cfg.GetString(GatekeeperModeKey, labels[0]));  
  PINDEX gkMode = (idx == P_MAX_INDEX) ? 0 : idx;
  rsrc->Add(new PHTTPRadioField(GatekeeperModeKey, labels, gkMode));

  // Gatekeeper 
  PString gkName = cfg.GetString(GatekeeperKey);
  rsrc->Add(new PHTTPStringField(GatekeeperKey, 25, gkName));

  // Gatekeeper UserName
  PString gkUserName = cfg.GetString(GatekeeperUserNameKey,"MCU");
  if (gkMode == Gatekeeper_None ) {
    // Local alias name for H.323 endpoint
    SetLocalUserName(cfg.GetString(LocalUserNameKey, OpenMCU::Current().GetName() + " v" + OpenMCU::Current().GetVersion()));
  } else {
    SetLocalUserName(gkUserName);
  }
  rsrc->Add(new PHTTPStringField(GatekeeperUserNameKey, 25, gkUserName));
  AliasList.AppendString(gkUserName);

  // Gatekeeper password
  PString gkPassword = PHTTPPasswordField::Decrypt(cfg.GetString(GatekeeperPasswordKey));
  SetGatekeeperPassword(gkPassword);
  rsrc->Add(new PHTTPPasswordField(GatekeeperPasswordKey, 25, gkPassword));

  // Gatekeeper Alias
  gkAlias = cfg.GetString(GatekeeperAliasKey,"MCU*");
  rsrc->Add(new PHTTPStringField(GatekeeperAliasKey, 25, gkAlias));

  for (PINDEX k=0; k< OpenMCU::defaultRoomCount; k++) {
	  PString alias = gkAlias;
	  alias.Replace("*",k);
	  AddAliasName(alias);   // Add the alias to the endpoint aliaslist
      AliasList.AppendString(alias);  
  }

  // Gatekeeper prefixes
  PINDEX prefixSize = cfg.GetInteger(PString(GatekeeperPrefixesKey) + " Array Size");
  if (prefixSize > 0) {
	  for (int j = 0; j < prefixSize; j++) {
	     PString prefix = cfg.GetString(psprintf("%s %u", GatekeeperPrefixesKey, j+1));
         PrefixList.AppendString(prefix);

		for (PINDEX k=0; k< OpenMCU::defaultRoomCount; k++) {
			PString alias = prefix + PString(k);
			AliasList.AppendString(alias);  
		}
	  }
  }
  rsrc->Add(new PHTTPFieldArray(new PHTTPStringField(cfg.GetDefaultSection() + "\\" + GatekeeperPrefixesKey, 
	                                                                    GatekeeperPrefixesKey, 20, ""), FALSE));

#if OPENMCU_VIDEO
  enableVideo = cfg.GetBoolean(EnableVideoKey, TRUE);
  rsrc->Add(new PHTTPBooleanField(EnableVideoKey, enableVideo));

  videoRate = cfg.GetInteger(VideoFrameRateKey, DefaultVideoFrameRate);
  rsrc->Add(new PHTTPIntegerField(VideoFrameRateKey, 1, 30, videoRate,"For outgoing video: 1..30"));

  videoTxQuality = cfg.GetInteger(VideoQualityKey, DefaultVideoQuality);
  rsrc->Add(new PHTTPIntegerField(VideoQualityKey, 1, 30, videoTxQuality));
#endif


  capabilities.RemoveAll();

  FILE *capCfg;
  int capsNum=0;
  capCfg = fopen("capability.conf","rt");
  if(capCfg!=NULL)
  {
   while(fscanf(capCfg,"%*[^\n]")!=EOF)
   {
    capsNum++;
    fscanf(capCfg,"%*[\n]");
   }
   fclose(capCfg);
   rsCaps = (char **)calloc(capsNum,sizeof(char *));
   tsCaps = (char **)calloc(capsNum,sizeof(char *));
   rvCaps = (char **)calloc(capsNum,sizeof(char *));
   tvCaps = (char **)calloc(capsNum,sizeof(char *));
   cvCaps = (char **)calloc(capsNum,sizeof(char *));
   listCaps = (char *)calloc(capsNum,64*sizeof(char));
   capCfg = fopen("capability.conf","rt");
   int capsType=0;
   char buf[64];
   capsNum=0;
   int listNum=0;
   while(fscanf(capCfg,"%64[^ #\t\n]",buf)!=EOF)
   {
    if(strcmp(buf,"[RECEIVE_SOUND]")==0) { capsType=1; listNum=0; }
    else
    if(strcmp(buf,"[TRANSMIT_SOUND]")==0) { capsType=2; listNum=0; }
    else
    if(strcmp(buf,"[RECEIVE_VIDEO]")==0) { capsType=3; listNum=0; }
    else
    if(strcmp(buf,"[TRANSMIT_VIDEO]")==0) { capsType=4; listNum=0; }
    else
    if(strcmp(buf,"[TRANSMIT_VIDEOCACHE]")==0) { capsType=5; listNum=0; }
    else if(capsType!=0 && *buf!=0)
    {
     if(capsType==1) { rsCaps[listNum]=&(listCaps[64*capsNum]); listNum++; }
     else if(capsType==2) { tsCaps[listNum]=&(listCaps[64*capsNum]); listNum++; }
     else if(capsType==3) { rvCaps[listNum]=&(listCaps[64*capsNum]); listNum++; }
     else if(capsType==4) { tvCaps[listNum]=&(listCaps[64*capsNum]); listNum++; }
     else if(capsType==5) { cvCaps[listNum]=&(listCaps[64*capsNum]); listNum++; }
     strcpy(&(listCaps[64*capsNum]),buf); capsNum++;
    }
    fscanf(capCfg,"%*[^\n]"); fscanf(capCfg,"%*[\n]");
   }
   fclose(capCfg);

   cout << "[RECEIVE_SOUND]= "; listNum=0; 
   while(rsCaps[listNum]!=NULL) { cout << rsCaps[listNum] << ", "; listNum++; }
   cout << "\n";
   cout << "[TRANSMIT_SOUND]= "; listNum=0; 
   while(tsCaps[listNum]!=NULL) { cout << tsCaps[listNum] << ", "; listNum++; }
   cout << "\n";
   cout << "[RECEIVE_VIDEO]= "; listNum=0; 
   while(rvCaps[listNum]!=NULL) { cout << rvCaps[listNum] << ", "; listNum++; }
   cout << "\n";
   cout << "[TRANSMIT_VIDEO]= "; listNum=0; 
   while(tvCaps[listNum]!=NULL) { cout << tvCaps[listNum] << ", "; listNum++; }
   cout << "\n";
   cout << "[TRANSMIT_VIDEOCACHE]= "; listNum=0; 
   while(cvCaps[listNum]!=NULL) { cout << cvCaps[listNum] << ", "; listNum++; }
   cout << "\n";
   AddCapabilities(0,0,(const char **)rsCaps);
   AddCapabilities(0,1,(const char **)rvCaps);
   cout << capabilities;
  }
  else
  {
   AddAllCapabilities(0, 0, "*");
   AddAllExtendedVideoCapabilities(0, 2);
  
   // disable codecs as required
   PString disableCodecs = cfg.GetString(DisableCodecsKey);
   rsrc->Add(new PHTTPStringField(DisableCodecsKey, 50, disableCodecs));
   if (!disableCodecs.IsEmpty()) {
     PStringArray toRemove = disableCodecs.Tokenise(',', FALSE);
     capabilities.Remove(toRemove);
   }
   AddAllUserInputCapabilities(0, 3);
  }

#if 0 //  old MCU options
  int videoTxQual = 10;
  if (args.HasOption("videotxquality")) 
      videoTxQual = args.GetOptionString("videotxquality").AsInteger();
  endpoint.videoTxQuality = PMAX(1, PMIN(31, videoTxQual));

  int videoF = 2;
  if (args.HasOption("videofill")) 
    videoF = args.GetOptionString("videofill").AsInteger();
  endpoint.videoFill = PMAX(1, PMIN(99, videoF));

  int videoFPS = 10;
  if (args.HasOption("videotxfps")) 
    videoFPS = args.GetOptionString("videotxfps").AsInteger();
  endpoint.videoFramesPS = PMAX(1,PMIN(30,videoFPS));

  int videoBitRate = 0; //disable setting videoBitRate.
  if (args.HasOption("videobitrate")) {
    videoBitRate = args.GetOptionString("videobitrate").AsInteger();
    videoBitRate = 1024 * PMAX(16, PMIN(2048, videoBitRate));
  }
  endpoint.videoBitRate = videoBitRate;
#endif


  switch (gkMode) {
    default:
    case Gatekeeper_None:
      break;

    case Gatekeeper_Find:
      if (!DiscoverGatekeeper(new H323TransportUDP(*this)))
        PSYSTEMLOG(Error, "No gatekeeper found");
	  else
		PSYSTEMLOG(Info, "Found Gatekeeper: " << gatekeeper);
      break;

    case Gatekeeper_Explicit:
      if (!SetGatekeeper(gkName, new H323TransportUDP(*this)))
        PSYSTEMLOG(Error, "Error registering with gatekeeper at \"" << gkName << '"');
	  else
		PSYSTEMLOG(Info, "Registered with Gatekeeper: " << gkName);
  }

  PTRACE(2, "MCU\tCodecs (in preference order):\n" << setprecision(2) << GetCapabilities());;
}

H323Connection * OpenMCUH323EndPoint::CreateConnection(
      unsigned callReference,
      void * userData,
      H323Transport *,
      H323SignalPDU *)

{
  return new OpenMCUH323Connection(*this, callReference, userData);
}

void OpenMCUH323EndPoint::TranslateTCPAddress(PIPSocket::Address &localAddr, const PIPSocket::Address &remoteAddr)
{
  if (this->behind_masq)
  {
    BYTE byte1=localAddr.Byte1();
    BYTE byte2=localAddr.Byte2();
    const BOOL local_mcu=(
       (byte1==10)                       // LAN class A
     ||((byte1==172)&&((byte2&240)==16)) // LAN class B
     ||((byte1==192)&&(byte2==168))      // LAN class C
     ||((byte1==169)&&(byte2==254))      // APIPA/IPAC/zeroconf (probably LAN)
    );
    if(!local_mcu){
      PTRACE(6,"h323.cxx(openmcu)\tTranslateTCPAddress: remoteAddr=" << remoteAddr << ", localAddr=" << localAddr << " - not changed (source is GLOBAL)");
      return;
    }
    byte1=remoteAddr.Byte1();
    byte2=remoteAddr.Byte2();
    if((byte1==10)                       // LAN class A
     ||((byte1==172)&&((byte2&240)==16)) // LAN class B
     ||((byte1==192)&&(byte2==168))      // LAN class C
     ||((byte1==169)&&(byte2==254))      // APIPA/IPAC/zeroconf (probably LAN)
    ) PTRACE(6,"h323.cxx(openmcu)\tTranslateTCPAddress: remoteAddr=" << remoteAddr << ", localAddr=" << localAddr << " - not changed (destination is LAN)");
    else {
      PTRACE(6,"h323.cxx(openmcu)\tTranslateTCPAddress: remoteAddr=" << remoteAddr << ", localAddr=" << localAddr << " ***changing to " << *(this->masqAddressPtr));
      localAddr=*(this->masqAddressPtr);
    }
  } else PTRACE(6,"h323.cxx(openmcu)\tTranslateTCPAddress: remoteAddr=" << remoteAddr << ", localAddr=" << localAddr << " - nothing changed ('NAT Router IP' not configured)");
  return;
}

PString OpenMCUH323EndPoint::GetRoomStatus(const PString & block)
{
  PString substitution;
  PWaitAndSignal m(conferenceManager.GetConferenceListMutex());
  ConferenceListType & conferenceList = conferenceManager.GetConferenceList();

  ConferenceListType::iterator r;
  for (r = conferenceList.begin(); r != conferenceList.end(); ++r) {

    // make a copy of the repeating html chunk
    PString insert = block;
    PStringStream members;
    members << 
	"<table class=\"table table-striped table-bordered table-condensed\">"
               "<tr>"
                 "<th>"
                 "&nbsp;Name&nbsp;"
                 "</th><th>"
                 "&nbsp;Duration&nbsp;"
                 "</th><th>"
                 "&nbsp;Codec&nbsp;"
                 "</th><th>"
                 "&nbsp;RTP Packets/Bytes tx&nbsp;"
                 "</th><th>"
                 "&nbsp;RTP Packets/Bytes rx&nbsp;"
                 "</th>"
#if OPENMCU_VIDEO
                 "<th>"
//                 "&nbsp;TX&nbsp;Video&nbsp;frame&nbsp;rate/<br>"
                 "&nbsp;RX&nbsp;Video&nbsp;frame&nbsp;rate&nbsp;"
                 "</th>"
#endif
                 "</tr>";

    Conference & conference = *(r->second);
    size_t memberNameListSize = 0;
    {
      PWaitAndSignal m(conference.GetMutex());
      Conference::MemberNameList & memberNameList = conference.GetMemberNameList();
      memberNameListSize = memberNameList.size();
      Conference::MemberNameList::const_iterator s;
      for (s = memberNameList.begin(); s != memberNameList.end(); ++s) 
      {
        ConferenceMember * member = s->second;
        if(member == NULL) {members << "<tr><td>" << s->first << "</td></tr>"; continue; }
        H323Connection_ConferenceMember * connMember = dynamic_cast<H323Connection_ConferenceMember *>(member);
        if (connMember != NULL) {
          OpenMCUH323Connection * conn = (OpenMCUH323Connection *)FindConnectionWithLock(connMember->GetH323Token());
          if (conn != NULL) {
            PTime now;
            PTime callStart = conn->GetConnectionStartTime();
            RTP_Session * session = conn->GetSession(RTP_Session::DefaultAudioSessionID);
            RTP_Session * v_session = conn->GetSession(RTP_Session::DefaultVideoSessionID);
            if(session!=NULL && v_session!=NULL)
            {
members << "<tr>"
                      "<td>"
                    << s->first
                    << (member->IsMCU() ? "<BR>(MCU)" : "")

                    << "</td><td>"   
                    << (now -callStart)
                    << "</td><td>"   
                    << conn->GetAudioTransmitCodecName() << '/' << conn->GetAudioReceiveCodecName()
#if OPENMCU_VIDEO
                    << "<BR>" << conn->GetVideoTransmitCodecName() << " / " << conn->GetVideoReceiveCodecName()
                      << "@" << connMember->GetVideoRxFrameSize()
#endif
                    << "</td><td>"   
                    << session->GetPacketsSent() << '/' << session->GetOctetsSent() 
#if OPENMCU_VIDEO
                    << "<BR>" << v_session->GetPacketsSent() << '/' << v_session->GetOctetsSent()
#endif
                    << "</td><td>"   
                    << session->GetPacketsReceived() << '/' << session->GetOctetsReceived()
#if OPENMCU_VIDEO
                    << "<BR>" << v_session->GetPacketsReceived() << '/' << v_session->GetOctetsReceived()
#endif
                    << "</td>";
            }        
            conn->Unlock();
          }
        }
        members 
#if OPENMCU_VIDEO
                << "<td>"   
//                << member->GetVideoTxFrameRate() << '/' 
                << member->GetVideoRxFrameRate()
                << "</td>"   
#endif
                << "</tr>";
      }
/*
      Conference::MemberNameList & serviceMemberNameList = conference.GetServiceMemberNameList();
      for (s = serviceMemberNameList.begin(); s != serviceMemberNameList.end(); ++s) 
      {
        ConferenceMember * member = s->second;
        if(member == NULL) {members << "<tr><td>" << s->first << "</td></tr>"; continue; }
        H323Connection_ConferenceMember * connMember = dynamic_cast<H323Connection_ConferenceMember *>(member);
        if (connMember != NULL) {
          OpenMCUH323Connection * conn = (OpenMCUH323Connection *)FindConnectionWithLock(connMember->GetH323Token());
          if (conn != NULL) {
            PTime now;
            PTime callStart = conn->GetConnectionStartTime();
            RTP_Session * v_session = conn->GetSession(RTP_Session::DefaultVideoSessionID);
            if(v_session!=NULL)
            {
members << "<tr>"
                      "<td>"
                    << s->first
                    << (member->IsMCU() ? "<BR>(MCU)" : "")

                    << "</td><td>"   
                    << (now -callStart)
                    << "</td><td>"   
                    << conn->GetAudioTransmitCodecName() << '/' << conn->GetAudioReceiveCodecName()
#if OPENMCU_VIDEO
                    << "<BR>" << conn->GetVideoTransmitCodecName()
#endif
                    << "</td><td>"   
#if OPENMCU_VIDEO
                    << "<BR>" << v_session->GetPacketsSent() << '/' << v_session->GetOctetsSent()
#endif
                    << "</td><td>"   
#if OPENMCU_VIDEO
                    << "<BR>" << v_session->GetPacketsReceived() << '/' << v_session->GetOctetsReceived()
#endif
                    << "</td>";
            }        
            conn->Unlock();
          }
        }
        members 
#if OPENMCU_VIDEO
                << "<td>"   
//                << member->GetVideoTxFrameRate() << '/' 
                << member->GetVideoRxFrameRate()
                << "</td>"   
#endif
                << "</tr>";
      }
*/      
    }
    members << "</table>";
    SpliceMacro(insert, "RoomName",        conference.GetNumber());
    SpliceMacro(insert, "RoomMemberCount", PString(PString::Unsigned, memberNameListSize));
    SpliceMacro(insert, "RoomMembers",     members);
    substitution += insert;
  }

  return substitution;
}
/*
PString OpenMCUH323EndPoint::GetMemberList(Conference & conference, ConferenceMemberId id)
{
 PStringStream members;
 size_t memberListSize = 0;
 PWaitAndSignal m(conference.GetMutex());
 Conference::MemberList & memberList = conference.GetMemberList();
 memberListSize = memberList.size();
 Conference::MemberList::const_iterator s;
 members << "<option value=\"0\"></option>";
 members << "<option" << ((id==(void *)(-1))?" selected ":" ") << "value=\"-1\">VAD</option>";
 members << "<option" << ((id==(void *)(-2))?" selected ":" ") << "value=\"-2\">VAD2</option>";
 for (s = memberList.begin(); s != memberList.end(); ++s) 
 {
  ConferenceMember * member = s->second;
  if(member->GetName()=="file recorder") continue;
  int mint[1];
  ConferenceMemberId *mid=(ConferenceMemberId *) mint;
  mid[0]=member->GetID();
  members << "<option"
	<< ((member->GetID()==id)?" selected ":" ")
	<< "value=\"" << mint[0] << "\">"
	<< member->GetName() << "</option>";
 }

 return members;
}
*/

PString OpenMCUH323EndPoint::GetMemberList(Conference & conference, ConferenceMemberId id)
{
 PStringStream members;
 PWaitAndSignal m(conference.GetMutex());
 Conference::MemberNameList & memberNameList = conference.GetMemberNameList();
 Conference::MemberNameList::const_iterator s;
 members << "<option value=\"0\"></option>";
 members << "<option" << ((id==(void *)(-1))?" selected ":" ") << "value=\"-1\">VAD</option>";
 members << "<option" << ((id==(void *)(-2))?" selected ":" ") << "value=\"-2\">VAD2</option>";
 for (s = memberNameList.begin(); s != memberNameList.end(); ++s) 
 {
  ConferenceMember * member = s->second;
  if(member==NULL) continue;
//  int mint[1];
//  ConferenceMemberId *mid=(ConferenceMemberId *) mint;
//  mid[0]=member->GetID();
//  if(mint[0]!=0)
  long mint=(long)member->GetID();
  if(mint!=0)
  {
   PString username=s->first;
   members << "<option"
//	<< ((mid[0]==id)?" selected ":" ")
//	<< "value=\"" << mint[0] << "\">"
	<< ((mint==(long)id)?" selected ":" ")
	<< "value=\"" << mint << "\">"
	<< username << "</option>";
  }
 }
 return members;
}

BOOL OpenMCUH323EndPoint::MemberExist(Conference & conference, ConferenceMemberId id)
{
 PWaitAndSignal m(conference.GetMutex());
 Conference::MemberNameList & memberNameList = conference.GetMemberNameList();
 Conference::MemberNameList::const_iterator s;
 for (s = memberNameList.begin(); s != memberNameList.end(); ++s) 
 {
  ConferenceMember * member = s->second;
  if(member==NULL) continue;
  if(member->GetID()==id) return true;
 }
 PTRACE(6,"!MEMBER_EXIST_MATCH!\tid=" << id << " conference=" << conference);
 return false;
}

PString OpenMCUH323EndPoint::GetMemberListOpts(Conference & conference)
{
 PStringStream members;
// size_t memberListSize = 0;
 PWaitAndSignal m(conference.GetMutex());
 Conference::MemberNameList & memberNameList = conference.GetMemberNameList();
 Conference::MemberNameList::const_iterator s;
 members << "<table class=\"table table-striped table-bordered table-condensed\"><tr><td valign=top style='padding-left:12px'>";
 members << "<font color=green><b>Active Members</b></font>";
 members << "<table class=\"table table-striped table-bordered table-condensed\">";
 members << "<tr><th>Active Members<th>Mute voice<th>Disable VAD<th>Chosen Van<th>Drop"; 
 int i=0;
 for (s = memberNameList.begin(); s != memberNameList.end(); ++s) 
 {
  ConferenceMember * member = s->second;
  if(member==NULL) continue;
//  long mint[1];
//  ConferenceMemberId *mid=(ConferenceMemberId *) mint;
//  mid[0]=member->GetID();
  long mint=(long)member->GetID();
   members << "<tr>"; 
  members << "<th id=\"tam_" << i << "\" >";
  members << s->first;
  members << "<th>";
//  members << "<input type=\"checkbox\" name=\"m" << mint[0] << "\" value=\"+\" " << ((member->muteIncoming)?"checked":"") << ">";
  members << "<input type=\"checkbox\" name=\"m" << mint << "\" value=\"+\" " << ((member->muteIncoming)?"checked":"") << ">";
  members << "<th>";
//  members << "<input type=\"checkbox\" name=\"v" << mint[0] << "\" value=\"+\" " << ((member->disableVAD)?"checked":"") << ">";
  members << "<input type=\"checkbox\" name=\"v" << mint << "\" value=\"+\" " << ((member->disableVAD)?"checked":"") << ">";
  members << "<th>";
//  members << "<input type=\"checkbox\" name=\"c" << mint[0] << "\" value=\"+\" " << ((member->chosenVan)?"checked":"") << ">";
  members << "<input type=\"checkbox\" name=\"c" << mint << "\" value=\"+\" " << ((member->chosenVan)?"checked":"") << ">";
  members << "<th>";
//  members << "<input type=\"checkbox\" name=\"d" << mint[0] << "\" value=\"+\">";
  members << "<input type=\"checkbox\" name=\"d" << mint << "\" value=\"+\">";
  i++;
 }
 members << "<tr>"; 
 members << "<th>";
 members << "ALL Active!!!";
 members << "<th>";
// members << "<input type=\"checkbox\" name=\"m" << mint[0] << "\" value=\"+\" " << ((member->muteIncoming)?"checked":"") << ">";
 members << "<th>";
// members << "<input type=\"checkbox\" name=\"v" << mint[0] << "\" value=\"+\" " << ((member->disableVAD)?"checked":"") << ">";
 members << "<th>";
// members << "<input type=\"checkbox\" name=\"c" << mint[0] << "\" value=\"+\" " << ((member->chosenVan)?"checked":"") << ">";
 members << "<th>";
 members << "<input type=\"checkbox\" name=\"d" << "ALL" << "\" value=\"+\">";
 members  << "</table>";

 members << "</td><td valign=top style='padding-right:12px'>";

 members << "<font color=red><b>Inactive Members</b></font>";
 members << "<table class=\"table table-striped table-bordered table-condensed\">";
 members << "<tr><th>Inactive Members<th>Remove<th>Invite"; 
 members << "<tr>"; 
 members << "<th>";
 members << "ALL Inactive!!!";
 members << "<th>";
 members << "<input type=\"checkbox\" name=\"r" << "ALL" << "\" value=\"+\">";
 members << "<th>";
 members << "<input type=\"checkbox\" name=\"i" << "ALL" << "\" value=\"+\">";
 i=0;
 for (s = memberNameList.begin(); s != memberNameList.end(); ++s) 
 {
  ConferenceMember * member = s->second;
  if(member!=NULL) continue;
  members << "<tr>"; 
  members << "<th id=\"tim_" << i << "\">";
  members << s->first;
  members << "<th>";
  members << "<input type=\"checkbox\" name=\"r" << s->first << "\" value=\"+\">";
  members << "<th>";
  members << "<input id=\"iinv_" << i << "\" type=\"checkbox\" name=\"i" << s->first << "\" value=\"+\">";
  i++;
 }
 members  << "</table>";
 members << "</td></table>";

 return members;
}

PString OpenMCUH323EndPoint::GetMemberListOptsJavascript(Conference & conference)
{
 PStringStream members;
 PWaitAndSignal m(conference.GetMutex());
 Conference::MemberNameList & memberNameList = conference.GetMemberNameList();
 Conference::MemberNameList::const_iterator s;
 members << "members=Array(";
 int i=0;
 for (s = memberNameList.begin(); s != memberNameList.end(); ++s) 
 {
  PString username=s->first;
  username.Replace("&","&amp;",TRUE,0);
  username.Replace("\"","&quot;",TRUE,0);
  ConferenceMember * member = s->second;
  if(member==NULL){ // inactive member
    if(i>0) members << ",";
    members << "Array(0"
      << ",0"
      << ",\"" << username << "\""
      << ",0"
      << ",0"
      << ",0"
      << ",0"
      << ")";
    i++;
  } else {          //   active member
    if(i>0) members << ",";
    members << "Array(1"
      << ",\"" << dec << (long)member->GetID() << "\""
//      << ",\"" << s->first << "\""
      << ",\"" << username << "\""
      << "," << member->muteIncoming
      << "," << member->disableVAD
      << "," << member->chosenVan
      << "," << member->GetAudioLevel()
      << ")";
    i++;
  }
 }
 members << ");";
 PTRACE(6,"GetMemberListOptsJavascript\t" << members);
 return members;
}

PString OpenMCUH323EndPoint::SetMemberOptionOTF(const PString room,const PStringToString & data)
{
  if(!data.Contains("mid")) return "ERROR: &mid does not set"; if(!data.Contains("action")) return "ERROR: &action does not set";
  int action=data("action").AsInteger();

  if(action>=64) { // MEMBERLIST CONTROL:
    PTRACE(6,"OTFControl\troom=" << room << "; action=" << action);
    PWaitAndSignal m2(conferenceManager.GetConferenceListMutex());
    ConferenceListType & conferenceList = conferenceManager.GetConferenceList();
    ConferenceListType::iterator r;
    for (r = conferenceList.begin(); r != conferenceList.end(); ++r) if(r->second->GetNumber() == room) break;
    if(r == conferenceList.end() ) return "Bad room";
    Conference & conference = *(r->second);
    PWaitAndSignal m(conference.GetMutex());
    switch(action){
      case 64: // Drop ALL active connections
      {
        Conference::MemberList & memberList = conference.GetMemberList();
        Conference::MemberList::const_iterator s;
        int i = memberList.size(); s = memberList.end(); s--; while(i!=0) {
          ConferenceMember * member = s->second; s--; i--;
          if(member->GetName()=="file recorder") continue;
          memberList.erase(member->GetID()); member->Close();
        }
        return "OK";
      } break;
      case 65: // Invite ALL inactive members
      {
        Conference::MemberNameList & memberNameList = conference.GetMemberNameList();
        Conference::MemberNameList::const_iterator s;
        for (s = memberNameList.begin(); s != memberNameList.end(); ++s) if(s->second==NULL) {
//          PTRACE(6,"OTF\tInviting " << s->first << " as offline member from the list");
          conference.InviteMember(s->first);
        }
        return "OK";
      } break;
      case 66: // Remove ALL inactive members
      {
        Conference::MemberNameList & memberNameList = conference.GetMemberNameList();
        Conference::MemberNameList::const_iterator s;
        for (s = memberNameList.begin(); s != memberNameList.end(); ++s) if(s->second==NULL) conference.RemoveOfflineMemberFromNameList((PString &)(s->first));
        return "OK";
      } break;
      case 67: // Save members.conf
      {
        Conference::MemberNameList & memberNameList = conference.GetMemberNameList();
        Conference::MemberNameList::const_iterator s;
        FILE *membLst;
        PString name="members_"+room+".conf";
//        membLst = fopen("members.conf","w");
        membLst = fopen(name,"w");
        if(membLst!=NULL){
          for (s = memberNameList.begin(); s != memberNameList.end(); ++s) fputs(s->first+"\n",membLst);
          fclose(membLst);
        }
        return "OK";
      } break;
#if USE_LIBYUV
      case 68: // libyuv filtering control
      {
        long mid=data("mid").AsInteger();
        if(mid==1) OpenMCU::Current().SetScaleFilter(libyuv::kFilterBilinear);
        else if(mid==2) OpenMCU::Current().SetScaleFilter(libyuv::kFilterBox);
        else OpenMCU::Current().SetScaleFilter(libyuv::kFilterNone);
        return "OK";
      } break;
#endif
    }
    return "OK";

  } else if(action < 32) { // ONLINE MEMBER CONTROL:
    long mid_otf=data("mid").AsInteger(); PTRACE(6,"OTFControl\troom=" << room << "; mid=" << mid_otf << "; action=" << action);
    PWaitAndSignal m2(conferenceManager.GetConferenceListMutex());
    ConferenceListType & conferenceList = conferenceManager.GetConferenceList();
    ConferenceListType::iterator r;
    for (r = conferenceList.begin(); r != conferenceList.end(); ++r) if(r->second->GetNumber() == room) break;
    if(r == conferenceList.end() ) return "Bad room";
    Conference & conference = *(r->second);
    PWaitAndSignal m(conference.GetMutex());
    Conference::MemberList & memberList = conference.GetMemberList();
    Conference::MemberList::const_iterator s;
    for (s = memberList.begin(); s != memberList.end(); ++s) {
      ConferenceMember * member = s->second;
      if(member->GetName()=="file recorder") continue;
      ConferenceMemberId mid = member->GetID();
      if((long)mid==mid_otf) switch(action){
        case 0: member->muteIncoming=FALSE; return "OK"; break;
        case 1: member->muteIncoming=TRUE; return "OK"; break;
        case 2: member->disableVAD=FALSE; return "OK"; break;
        case 3: member->disableVAD=TRUE; return "OK"; break;
        case 4: member->chosenVan=FALSE; return "OK"; break;
        case 5: member->chosenVan=TRUE; return "OK"; break;
        case 7: memberList.erase(member->GetID()); member->Close(); return "OK"; break;
        case 8: member->disableVAD=FALSE; member->chosenVan=FALSE; return "OK"; break;
        case 9: member->disableVAD=FALSE; member->chosenVan=TRUE; return "OK"; break;
        case 10: member->disableVAD=TRUE; member->chosenVan=FALSE; return "OK"; break;
      }
    }
    return "OK";

  }; // OFFLINE MEMBER CONTROL:
  PString mid_otf=data("mid"); PTRACE(6,"OTFControl\troom=" << room << "; mid=" << mid_otf << "; action=" << action);
  PWaitAndSignal m2(conferenceManager.GetConferenceListMutex());
  ConferenceListType & conferenceList = conferenceManager.GetConferenceList();
  ConferenceListType::iterator r;
  for (r = conferenceList.begin(); r != conferenceList.end(); ++r) if(r->second->GetNumber() == room) break;
  if(r == conferenceList.end() ) return "Bad room";
  Conference & conference = *(r->second);
  PWaitAndSignal m(conference.GetMutex());
  Conference::MemberNameList & memberNameList = conference.GetMemberNameList();
  Conference::MemberNameList::const_iterator s;
  for (s = memberNameList.begin(); s != memberNameList.end(); ++s) {
    ConferenceMember * member = s->second;
    if(member!=NULL) continue; //online member
    PString arg = s->first;
    if(arg==mid_otf) switch(action){
      case 32: conference.InviteMember(arg); return "OK"; break;
      case 33: conference.RemoveOfflineMemberFromNameList(arg); return "OK"; break;
    }
  }

  return "OK";
}

void OpenMCUH323EndPoint::SetMemberListOpts(Conference & conference,const PStringToString & data)
{
 PWaitAndSignal m(conference.GetMutex());
 Conference::MemberList & memberList = conference.GetMemberList();
 Conference::MemberList::const_iterator s;
 for (s = memberList.begin(); s != memberList.end(); ++s) 
 {
  ConferenceMember * member = s->second;
  if(member->GetName()=="file recorder") continue;
  ConferenceMemberId mid = member->GetID();
  
  PString arg = (long)mid;
  PString arg1 = "m" + arg;
  PString opt = data(arg1);
  member->muteIncoming = (opt == "+")?TRUE:FALSE; 
  arg1 = "v" + arg;
  opt = data(arg1);
  member->disableVAD = (opt == "+")?TRUE:FALSE; 
  arg1 = "c" + arg;
  opt = data(arg1);
  member->chosenVan = (opt == "+")?1:0; 
 }
/* 
 for (s = memberList.begin(); s != memberList.end(); ++s) 
 {
  ConferenceMember * member = s->second;
  if(member->GetName()=="file recorder") continue;
  ConferenceMemberId mid = member->GetID();
  
  PString arg = (int)mid;
  PString arg1 = "d" + arg;
  PString opt = data(arg1);
  if(opt == "Drop") 
  { 
   conference.RemoveMember(member);
   member->Close();
//   member->WaitForClose();
   return; 
  }
 }
*/

 int i = memberList.size();
 s = memberList.end(); s--;
 PString dall = data("dALL");
 while(i!=0)
 {
  ConferenceMember * member = s->second;
  s--; i--;
  if(member->GetName()=="file recorder") continue;
  ConferenceMemberId mid = member->GetID();
  
  PString arg = (long)mid;
  PString arg1 = "d" + arg;
  PString opt = data(arg1);
  if(opt == "+" || dall == "+") 
  {
//   PThread::Sleep(500);
//   conference.RemoveMember(member);
   memberList.erase(member->GetID());
   member->Close();
//   member->WaitForClose();
//   return; 
  }
 }

}

void OpenMCUH323EndPoint::OfflineMembersManager(Conference & conference,const PStringToString & data)
{
 PWaitAndSignal m(conference.GetMutex());
 Conference::MemberNameList & memberNameList = conference.GetMemberNameList();
 Conference::MemberNameList::const_iterator s;
 PString iall = data("iALL");
 for (s = memberNameList.begin(); s != memberNameList.end(); ++s) 
 {
  ConferenceMember * member = s->second;
  if(member!=NULL) continue; //online member
  PString arg = "i" + s->first;
  PString opt = data(arg);
  const char * name=s->first;
  if(opt=="+" || iall=="+") conference.InviteMember(name);
 }

 PString rall = data("rALL");
 for (s = memberNameList.begin(); s != memberNameList.end(); ) 
 {
  PString opt = data("r" + s->first);
  PString name = s->first;
  ConferenceMember * member = s->second;
  s++;
  if(member!=NULL) continue; //online member
  if(opt=="+" || rall=="+") 
    conference.RemoveOfflineMemberFromNameList(name);
 }
}


PString OpenMCUH323EndPoint::GetRoomList(const PString & block)
{
  PString substitution;
  PWaitAndSignal m(conferenceManager.GetConferenceListMutex());
  ConferenceListType & conferenceList = conferenceManager.GetConferenceList();

  PString insert = block;
  PStringStream members;
  members << "";
  ConferenceListType::iterator r;
  for (r = conferenceList.begin(); r != conferenceList.end(); ++r) {

    // make a copy of the repeating html chunk
    Conference & conference = *(r->second);
    members << "<input type=\"submit\" class=\"btn btn-large";
    if(conference.IsModerated()=="+") members << " btn-success";
    else members << " btn-primary";
    members << "\" name=\"room\" value=\"" << conference.GetNumber()// << conference.IsModerated()
	    << "\">";
  }
  members << "";
  SpliceMacro(insert, "List", members);
  substitution += insert;
  return substitution;
}

PString OpenMCUH323EndPoint::RoomCtrlPage(const PString room, BOOL ctrl, int n, Conference & conference, ConferenceMemberId *idp)
{
// int i,j;
 PStringStream page;

 BeginPage(page,"Room Control","Room Control","$CONTROL$");

 page << "<p>"
   << "<form  method=POST>"
   << "<input size=\"12\" type=\"text\" name=\"room\" value=\"" << room << "\"> "
   << "<input type=\"checkbox\" name=\"moderated\" value=\"+\" " << ((ctrl==TRUE)?"checked":"")
   << "> <b>Ctrl</b> "
   << "<span style='width:20px'>&nbsp;</span>"
   << "<input type=\"checkbox\" name=\"muteUnvisible\" value=\"+\" " << ((conference.IsMuteUnvisible())?"checked":"")
   << "><b> Mute invis.</b>"
//   << "&nbsp;<input type=button class=\"btn btn-success\" onclick=\"javascript:document.forms[0].sendit.click()\" value=\"Set\">"
   << "&nbsp;<input type=submit id='sendit' name='sendit' button class=\"btn btn-success\" value=\"Set\">"
#if USE_LIBYUV
   << "<span class=\"input-prepend\"><span style='padding-left:20px;'><span class=\"add-on\"><b>Flt: </b></span></span>"
    << "<span class=\"input-append\"><span class=\"add-on\" style=\"width:30px;background-color:#edd;overflow:hidden;text-align:center;cursor:pointer\" id=\"flt\" onclick=\""
     << "if(this.innerHTML=='None'){this.innerHTML='Bilinear';queue_otf_request(1,68);}"
     << "else if(this.innerHTML=='Bilinear'){this.innerHTML='Box';queue_otf_request(2,68);}"
     << "else {this.innerHTML='None';queue_otf_request(0,68);}"
     << "\">";
    libyuv::FilterMode filter=OpenMCU::Current().GetScaleFilter();
    if(filter==libyuv::kFilterBilinear) page << "Bilinear";
    else if(filter==libyuv::kFilterBox) page << "Box";
    else page << "None";
    page << "</span>"
#endif
//   << "&nbsp;"
   << "<span class=\"input-prepend\"><span style='padding-left:20px;'><span class=\"add-on\"><b>VAD: </b></span></span>"
    << "<span class=\"input-append\"><input type=\"text\" size=\"3\" name=\"VAlevel\" value=\"" << conference.VAlevel << "\"><span class=\"add-on\"> min. volume (0..65535)</span>"
    << "&nbsp;&nbsp;<input type=\"text\" size=\"3\" name=\"VAdelay\" value=\"" << conference.VAdelay << "\"><span class=\"add-on\"> delay (ms)</span>"
    << "&nbsp;&nbsp;<input type=\"text\" size=\"3\" name=\"VAtimeout\" value=\"" << conference.VAtimeout << "\"><span class=\"add-on\"> timeout (ms)</span>"
//       << "<input type=\"text\" name=\"echoLevel\" value=\"" << conference.echoLevel << "\">Echo level treshhold<p>"
    << "</span></span>"


   << "<br>"
   << "<div class=\"input-append\">"
   << "<a href=# class='btn btn-mini' onclick='javascript:{selvmn.selectedIndex=(selvmn.selectedIndex+selvmn.length-1)%selvmn.length;document.forms[0].sendit.click();}'>&lt;&lt;</a>"
   << "<select name=\"vidmemnum\" id=\"vidmemnum\">";
 for(unsigned ii=0;ii<OpenMCU::vmcfg.vmconfs;ii++)
 {
   if(((OpenMCU::vmcfg.vmconf[ii].splitcfg.mode_mask)&2))
   {
     page << "<option " << (((unsigned)n==ii)?"selected ":"") << "value=" << ii << ">" << OpenMCU::vmcfg.vmconf[ii].splitcfg.Id << "</option>";
   }
 }
 page
   << "</select>"
   << "<a href=# class='btn btn-mini' onclick='javascript:{selvmn.selectedIndex=(selvmn.selectedIndex+1)%selvmn.length;document.forms[0].sendit.click();}'>&gt;&gt;</a>"

   << " <span id=\"tags_here\">&nbsp;</span>"
   << "</p></div><br>";

 page << "<div style='text-align:center'><div style='margin-left:auto;margin-right:auto;width:" << OpenMCU::vmcfg.vmconf[n].splitcfg.mockup_width << "px;height:" << OpenMCU::vmcfg.vmconf[n].splitcfg.mockup_height << "px;background:#fff'>";
  page << "<div id='aaa' style='position:relative;left:0;top:0;width:0px;height:0px'>";
#if USE_LIBJPEG
  page << "<img style='position:absolute' id='mainframe' width="
    << OpenMCU::vmcfg.vmconf[n].splitcfg.mockup_width
    << " height="
    << OpenMCU::vmcfg.vmconf[n].splitcfg.mockup_height
    << " src='Jpeg?room=" << room << "'/>";
#endif
  page << "</div>";

  page << "<div id='logging0' style='position:relative;top:0px;left:-277px;width:0px;height:0px;opacity:0.7'>";
   page << "<div id='logging1' style='position:absolute;width:277px;height:300px'><iframe style='background-color:#eef;border:1px solid #55c;padding:0px;margin:0px' id='loggingframe' name='loggingframe' src='Comm?room=" << room << "' width=277 height=300>Your browser does not support IFRAME tag :(</iframe></div>";
  page << "</div>";


 page << "<div id='pbase' style='position:relative;left:0;top:0;width:0px;height:0px'></div>";
//  page << "<div id='pp_2' style='position:relative;top:0px;left:" << (OpenMCU::vmcfg.vmconf[n].splitcfg.mockup_width+4) << "px;width:0px;height:0px;opacity:0.7;filter:progid:DXImageTransform.Microsoft.Alpha(opacity=70)'>";
  page << "<div id='pp_2' style='position:relative;top:0px;left:" << (OpenMCU::vmcfg.vmconf[n].splitcfg.mockup_width+4) << "px;width:0px;height:0px;opacity:0.7'>";
   page << "<div onmouseover='ddover(event,this,-2)' onmouseout='ddout(event,this,-2)' id='members_pan' style='position:absolute;width:277px;height:" << OpenMCU::vmcfg.vmconf[n].splitcfg.mockup_height << "px'>...</div>";
  page << "</div>";

 for(unsigned ii=0;ii<OpenMCU::vmcfg.vmconf[n].splitcfg.vidnum;ii++)
 {
   page
     << "<div id='pp" << ii << "' style='position:relative;left:" << OpenMCU::vmcfg.vmconf[n].vmpcfg[ii].posx*OpenMCU::vmcfg.vmconf[n].splitcfg.mockup_width/OpenMCU::vmcfg.bfw << "px;"
     << "top:" << OpenMCU::vmcfg.vmconf[n].vmpcfg[ii].posy*OpenMCU::vmcfg.vmconf[n].splitcfg.mockup_height/OpenMCU::vmcfg.bfh << "px;width:0;height:0'>"
     << "<div onmousedown='ddstart(event,this," << ii << ",0)' onmouseover='ddover(event,this," << ii << ")' onmouseout='ddout(event,this," << ii << ")' style='overflow:hidden;opacity:0.5;filter:alpha(opacity=50);"
     << "position:absolute;background-color:#F2F2F2;text-align:center;border:1px solid #56BBD9;padding:0;cursor:move;"
     << "width:" << (OpenMCU::vmcfg.vmconf[n].vmpcfg[ii].width*OpenMCU::vmcfg.vmconf[n].splitcfg.mockup_width/OpenMCU::vmcfg.bfw-2) << "px;"
     << "height:" << (OpenMCU::vmcfg.vmconf[n].vmpcfg[ii].height*OpenMCU::vmcfg.vmconf[n].splitcfg.mockup_height/OpenMCU::vmcfg.bfh-2) << "px"
     << "'>";
   page
     << "<select onmouseover='prvnt=0' onmouseout='prvnt=1' onchange='uncheck_recall();form_gather_and_send();members_refresh()' name=\"usr" << ii << "\">"
     << GetMemberList(conference,idp[ii])
     << "</select><br>Position " << ii
     << "</div></div>";
 }



 PStringStream tags;
 for(unsigned ii=0;ii<OpenMCU::vmcfg.vmconfs;ii++) {
   tags << "tags[" << ii << "]=\"" << OpenMCU::vmcfg.vmconf[ii].splitcfg.tags << "\"; ";
 }

 page << "</div></div>";

//page << "</td><td width='50%' valign=top id='members_pan'>. . .</td></tr></table>";

// page << "<p><center>"
 page << "<center>"
   << " <a href=# onclick='javascript:rotate_positions(0)'>&lt;--</a> "
//   << "<input class=\"btn btn-success btn-large\" type=\"submit\" id=\"sendit\" name=\"sendit\" value=\"   Set   \">"
   << "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
   << " <a href=# onclick='javascript:rotate_positions(1)'>--&gt;</a></center> "
//   << "<p>"
//   << GetMemberListOpts(conference)
   << "<script language=\"javascript\"><!--\n"
   << "var " << GetMemberListOptsJavascript(conference) << "\n"
   << "var tags=Array(); " << tags << "if(window.handle_tags)handle_tags(\"all\"); "
#if USE_LIBJPEG
   << "function frameload(){frame=new Image(); frame.onload=function(){reframe();}; frame.src=''; frame.src='Jpeg?room=" << room << "&rand='+Math.random();};"
   << "function reframe(){document.getElementById('mainframe').src=frame.src; setTimeout(frameload,1999);};"
   << "frameload(); "
#endif
   << "//--></script>"
   << "</form>";
 EndPage(page,OpenMCU::Current().GetCopyrightText());
 return page;
}


void OpenMCUH323EndPoint::UnmoderateConference(Conference & conference)
{
 PWaitAndSignal m(conference.GetMutex());

 MCUVideoMixer * mixer = conference.GetVideoMixer();
 mixer->MyRemoveAllVideoSource();

 Conference::MemberList & memberList = conference.GetMemberList();
 Conference::MemberList::const_iterator s;
 for (s = memberList.begin(); s != memberList.end(); ++s) 
 {
  ConferenceMember * member = s->second;
  if(!member->IsVisible()) continue;
  if(mixer->AddVideoSource(member->GetID(), *member)) member->SetFreezeVideo(FALSE);
  else member->SetFreezeVideo(TRUE);
 }
}


PString OpenMCUH323EndPoint::SetRoomParams(const PStringToString & data)
{
  PTRACE(6,"SetRoomParams\tdata: " << data);
  PString substitution;
  PString room = data("room");

  BOOL xmlhttprequest=!data.Contains("sendit"); // XMLHTTPRequest

  // "On-the-Fly" Control via XMLHTTPRequest:
  if (xmlhttprequest) if(data.Contains("otfctrl")) return SetMemberOptionOTF(room,data);

  if(data.Contains("refresh")) // JavaScript data refreshing
  {
    PTRACE(6,"SetRoomParams\tJavascript data refresh");
    ConferenceListType::iterator r;
    PWaitAndSignal m(conferenceManager.GetConferenceListMutex());
    ConferenceListType & conferenceList = conferenceManager.GetConferenceList();
    for (r = conferenceList.begin(); r != conferenceList.end(); ++r) 
    {
      if(r->second->GetNumber() == room) break;
    }
    if(r == conferenceList.end() ) return "Bad room";
    Conference & conference = *(r->second);
    return GetMemberListOptsJavascript(conference);
  }

  if(!data.Contains("vidmemnum")) // Operator just entered
  {
    OpenMCU::Current().HttpWriteEventRoom("MCU Operator connected",room);
    PTRACE(6,"SetRoomParams\tOperator entrance detected");
    ConferenceListType::iterator r;
    PWaitAndSignal m(conferenceManager.GetConferenceListMutex());
    ConferenceListType & conferenceList = conferenceManager.GetConferenceList();
    for (r = conferenceList.begin(); r != conferenceList.end(); ++r) 
    {
      if(r->second->GetNumber() == room) break;
    }
    if(r == conferenceList.end() ) return "Bad room";
    Conference & conference = *(r->second);
    MCUVideoMixer * mixer = conference.GetVideoMixer();
    ConferenceMemberId idr[100];
    for(int i=0;i<100;i++) idr[i]=mixer->GetPositionId(i);
    return RoomCtrlPage(room,conference.IsModerated()=="+",mixer->GetPositionSet(),conference,idr);
  }

  PString mode = data("moderated");
  PString globalmute = data("muteUnvisible");
  PString pvidnum = data("vidmemnum");
  int vidnum = pvidnum.AsInteger();
//  int sqrv = sqrt(vidnum);

  long usr[100];
  ConferenceMemberId *idl=(ConferenceMemberId *) usr;
  ConferenceMemberId idr[100];
  
  for (long i=0;i<100;i++){
   PStringStream usrX;
   usrX << "usr" << i;
   PString usrXX=data(usrX);
   usr[i]=usrXX.AsInteger();
   idr[i]=idl[i];
  }

  PWaitAndSignal m(conferenceManager.GetConferenceListMutex());
  ConferenceListType & conferenceList = conferenceManager.GetConferenceList();

  ConferenceListType::iterator r;

  for (r = conferenceList.begin(); r != conferenceList.end(); ++r) 
  {
    if(r->second->GetNumber() == room) break;
//    if(r->second->GetNumber()+"+" == room) { room = r->second->GetNumber(); mode = "+"; break; }
//    if(r->second->GetNumber()+"-" == room) { room = r->second->GetNumber(); mode = "-"; break; }
  }
  if(r == conferenceList.end() ) return "Bad room";

  Conference & conference = *(r->second);

// this type of control was replaced by "on the fly" control:
//  OfflineMembersManager(conference,data);

  if(mode == "+") {
    conference.SetModerated(TRUE);
    conference.GetVideoMixer()->SetForceScreenSplit(TRUE);
  }
  else {
   conference.SetModerated(FALSE);
   conference.GetVideoMixer()->SetForceScreenSplit(OpenMCU::Current().GetForceScreenSplit());
   UnmoderateConference(conference);
//   return RoomCtrlPage(room,FALSE,sqrv,conference,idl);
   return RoomCtrlPage(room,FALSE,vidnum,conference,idl);
  }

  if(globalmute == "+") conference.SetMuteUnvisible(TRUE);
  else conference.SetMuteUnvisible(FALSE);

  PString pstr = data("VAdelay");
  conference.VAdelay = pstr.AsInteger(); 
  pstr = data("VAtimeout");
  conference.VAtimeout = pstr.AsInteger(); 
  pstr = data("VAlevel");
  conference.VAlevel = pstr.AsInteger(); 
  pstr = data("echoLevel");
  conference.echoLevel = pstr.AsInteger(); 

// this type of control was replaced by "on the fly" control:
//  if(!xmlhttprequest) SetMemberListOpts(conference,data);

  MCUVideoMixer * mixer = conference.GetVideoMixer();
  if(mixer->GetPositionSet()!=vidnum) // set has been changed, clear all pos
  {
    mixer->MyRemoveAllVideoSource();
  }
  else
  for(int i=0;i<100;i++)
  {
   ConferenceMemberId memberId = mixer->GetPositionId(i);
   if(memberId==idl[i]) idl[i]=NULL;
   else 
//   if(memberId==(void *)(-1) || memberId==(void *)(-2)) mixer->MyRemoveVideoSource(i,FALSE);
   if(memberId!=NULL) mixer->MyRemoveVideoSource(i,TRUE);
  }

  for(int i=0;i<100;i++) if(idl[i]!=NULL && idl[i]!=(void *)(-1) && idl[i]!=(void *)(-2))if(!MemberExist(conference,idl[i])) idl[i]=NULL;

  mixer->MyAddVideoSource(vidnum,idl);
  conference.PutChosenVan();
  conference.FreezeVideo(NULL);

//  return RoomCtrlPage(room,TRUE,sqrv,conference,idr);
  if (xmlhttprequest) return "OK";
  return RoomCtrlPage(room,TRUE,vidnum,conference,idr);
}



PString OpenMCUH323EndPoint::GetMonitorText()
{
  PStringStream output;

  PWaitAndSignal m(conferenceManager.GetConferenceListMutex());
  ConferenceListType & conferenceList = conferenceManager.GetConferenceList();

  output << "Room Count: " << (int)conferenceList.size() << "\n"
         << "Max Room Count: " << conferenceManager.GetMaxConferenceCount() << "\n";

  ConferenceListType::iterator r;
  PINDEX confNum = 0;
  for (r = conferenceList.begin(); r != conferenceList.end(); ++r) {

    Conference & conference = *(r->second);
    PWaitAndSignal m(conference.GetMutex());
    Conference::MemberList & memberList = conference.GetMemberList();

    output << "\n[Conference "     << ++confNum << "]\n"
           << "Title: "            <<  conference.GetNumber() << "\n"
           << "ID: "               << conference.GetID() << "\n"
           << "Duration: "         << (PTime() - conference.GetStartTime()) << "\n"
           << "Member Count: "     << (int)memberList.size() << "\n"
           << "Max Member Count: " << conference.GetMaxMemberCount() << "\n";
 
    Conference::MemberList::const_iterator s;
    PINDEX num = 0;
    for (s = memberList.begin(); s != memberList.end(); ++s) {
      ConferenceMember * member = s->second;
      if (member != NULL) {
        PStringStream hdr; hdr << "Member " << ++num << " ";
        output << hdr << "Title: " << member->GetTitle() << "\n"
               << hdr << "Duration: " << (PTime() - member->GetStartTime()) << "\n"
               << member->GetMonitorInfo(hdr);
      }
    }
  }

  return output;
}

PString OpenMCUH323EndPoint::GetUsername(ConferenceMemberId id)
{
  PStringStream output;
  PStringStream output2;
#ifndef _WIN32
  if(conferenceManager.GetConferenceListMutex().WillBlock()) {
    PTRACE(6,"GetUsername\tPossible deadlock, empty string will returned");
    return output;
  }
#endif
  PWaitAndSignal m(conferenceManager.GetConferenceListMutex());
  ConferenceListType & conferenceList = conferenceManager.GetConferenceList();

  ConferenceListType::iterator r;
  for (r = conferenceList.begin(); r != conferenceList.end(); ++r) {
    Conference & conference = *(r->second);
    {
#ifndef _WIN32
      if(conference.GetMutex().WillBlock()) {
        PTRACE(6,"GetUsername\tPreventing deadlock: empty string will returned");
        return output;
      }
#endif
      PWaitAndSignal m(conference.GetMutex());
      Conference::MemberNameList & memberNameList = conference.GetMemberNameList();
      Conference::MemberNameList::const_iterator s;
      for (s = memberNameList.begin(); s != memberNameList.end(); ++s) 
      {
        ConferenceMember * member = s->second;
        if(member != NULL) {
          if (member->GetID()==id){ output << s->first; return output; }
          else if(member==id) output2 << s->first;
        }
      }
    }
  }
  return output2;
}

BOOL OpenMCUH323EndPoint::OutgoingConferenceRequest(const PString & room)
{
  // create/find the conference
  BOOL stat = conferenceManager.MakeAndLockConference(room) != NULL;
  conferenceManager.UnlockConference();
  return stat;
}

PString OpenMCUH323EndPoint::IncomingConferenceRequest(H323Connection & connection, 
                                                  const H323SignalPDU & setupPDU)
{
  const H225_Setup_UUIE & setup = setupPDU.m_h323_uu_pdu.m_h323_message_body;

  /*
   Here is the algorithm used for joining a conference.

   - If the conference goal is e_invite, then refuse the call

   - if the conference ID matches the ID of an existing conference, then join that conference

   - If there is no destination address, join the default conference

   - If there is a destination address, try and match the destination address
     to a conference number. If there is a match, join to that conference

   - If the destination address does not match any conference, create a new conference
  */

  // get the conference ID from the incoming call
  OpalGloballyUniqueID conferenceID = setup.m_conferenceID;

  PString roomToJoin;

  // check the conference ID
  if (conferenceManager.HasConference(conferenceID, roomToJoin)) {
    PTRACE(3, "MCU\tUsing conference ID to join existing room " << roomToJoin);
    return roomToJoin;
  }

  // look at destination addresses
  PINDEX i;
  for (i = 0; (i < setup.m_destinationAddress.GetSize()); i++) {
    roomToJoin = H323GetAliasAddressString(setup.m_destinationAddress[i]);
    if (conferenceManager.HasConference(roomToJoin)) {
      PTRACE(3, "MCU\tJoining room specified by destination address " << roomToJoin);
      return roomToJoin;
    }
  }

  // look at Q931 called party number
  if (roomToJoin.IsEmpty() && !setupPDU.GetQ931().GetCalledPartyNumber(roomToJoin) && roomToJoin.IsEmpty()) {
    if (conferenceManager.HasConference(roomToJoin)) {
      PTRACE(3, "MCU\tJoining room specified by Q.931 called party " << roomToJoin);
      return roomToJoin;
    }
  }

  // if there is a room to create, then join this call to that conference
  if (roomToJoin.IsEmpty()) 
    roomToJoin = OpenMCU::Current().GetDefaultRoomName();

  if (!roomToJoin.IsEmpty()) {
    PTRACE(3, "MCU\tJoining default room " << roomToJoin);
    return roomToJoin;
  }

  PTRACE(3, "MCU\tRefusing call because no room specified, and no default room");
  return PString::Empty();
}


void OpenMCUH323EndPoint::OnIncomingSipConnection(PString &callToken, H323Connection &connection)
{
 connectionsMutex.Wait();
  PTRACE(3, "MCU\tSip connection");
 connectionsActive.SetAt(callToken, &connection);
 connectionsMutex.Signal();
}


///////////////////////////////////////////////////////////////

NotifyH245Thread::NotifyH245Thread(Conference & conference, BOOL _join, ConferenceMember * _memberToIgnore)
  : PThread(10000, AutoDeleteThread), join(_join), memberToIgnore(_memberToIgnore)
{ 
  mcuNumber  = conference.GetMCUNumber();
  terminalIdToSend = memberToIgnore->GetTerminalNumber();

  // create list of connections to notify
  Conference::MemberList::const_iterator r;
  for (r = conference.GetMemberList().begin(); r != conference.GetMemberList().end(); r++) {
    ConferenceMember * mbr = r->second;
    if (mbr != memberToIgnore) {
      H323Connection_ConferenceMember * h323Mbr = dynamic_cast<H323Connection_ConferenceMember *>(mbr);
      if (h323Mbr != NULL)
        tokens += h323Mbr->GetH323Token();
    }
  }

  Resume(); 
}



void NotifyH245Thread::Main()
{
  OpenMCUH323EndPoint & ep = OpenMCU::Current().GetEndpoint();

  // send H.245 message on each connection in turn
  PINDEX i;
  for (i = 0; i < tokens.GetSize(); ++i) {
    H323Connection * conn = ep.FindConnectionWithLock(tokens[i]);
    if (conn != NULL) {
      OpenMCUH323Connection * h323Conn = dynamic_cast<OpenMCUH323Connection *>(conn);

      H323ControlPDU pdu;
      H245_ConferenceIndication & ind = pdu.Build(H245_IndicationMessage::e_conferenceIndication);
      ind.SetTag(join ? H245_ConferenceIndication::e_terminalJoinedConference : H245_ConferenceIndication::e_terminalLeftConference);
      H245_TerminalLabel & terminalId = ind;
      terminalId.m_mcuNumber      = mcuNumber;
      terminalId.m_terminalNumber = terminalIdToSend;

      h323Conn->WriteControlPDU(pdu);

      h323Conn->Unlock();
    }
  }
}

///////////////////////////////////////////////////////////////

OpenMCUH323Connection::OpenMCUH323Connection(OpenMCUH323EndPoint & _ep, unsigned callReference, void * userData)
  : H323Connection(_ep, callReference), ep(_ep), isMCU(FALSE)
{
  conference       = NULL;
  conferenceMember = NULL;
  welcomeState     = NotStartedYet;

  if (userData != NULL) {
    requestedRoom    = *(PString *)userData;
    delete (PString *)userData;
  }

  audioReceiveCodecName = audioTransmitCodecName = "none";

#if OPENMCU_VIDEO
  videoGrabber = NULL;
  videoDisplay = NULL;
  videoReceiveCodecName = videoTransmitCodecName = "none";
  videoReceiveCodec = NULL;
  videoTransmitCodec = NULL;
#endif
}

OpenMCUH323Connection::~OpenMCUH323Connection()
{
 PThread::Sleep(500);
 connMutex.Wait();
}

void OpenMCUH323Connection::SetupCacheConnection(PString & format, Conference * conf, ConferenceMember * memb)
{
 remoteName = format;
 remotePartyName = format;
 conference = conf;
 conferenceIdentifier = conference->GetID();
 conferenceMember = (H323Connection_ConferenceMember *)memb; // fix it
 requestedRoom = conference->GetNumber();
}

BOOL OpenMCUH323Connection::OnReceivedSignalSetup(const H323SignalPDU & setupPDU)
{
  // get a good name from the other end
  const H225_Setup_UUIE & setup = setupPDU.m_h323_uu_pdu.m_h323_message_body;

  remoteName = setupPDU.GetQ931().GetDisplayName();
  if (remoteName.IsEmpty()) {
    if (setup.HasOptionalField(H225_Setup_UUIE::e_sourceAddress))
      remoteName = H323GetAliasAddressString(setup.m_sourceAddress[0]);
  }

  if (remoteName.IsEmpty()) {
    if (!setupPDU.GetQ931().GetCallingPartyNumber(remoteName))
      remoteName.MakeEmpty();
  }

  if (remoteName.IsEmpty())
    remoteName = GetRemotePartyName();

  isMCU = setup.m_sourceInfo.m_mc;

  return H323Connection::OnReceivedSignalSetup(setupPDU);
}

BOOL OpenMCUH323Connection::OnReceivedCallProceeding(const H323SignalPDU & proceedingPDU)
{
  const H225_CallProceeding_UUIE & proceeding = proceedingPDU.m_h323_uu_pdu.m_h323_message_body;
  isMCU = proceeding.m_destinationInfo.m_mc;
  return H323Connection::OnReceivedCallProceeding(proceedingPDU);
}

void OpenMCUH323Connection::CleanUpOnCallEnd()
{
  PTRACE(1, "OpenMCUH323Connection\tCleanUpOnCallEnd");
  videoReceiveCodecName = videoTransmitCodecName = "none";
  videoReceiveCodec = NULL;
  videoTransmitCodec = NULL;
  LeaveConference();

  H323Connection::CleanUpOnCallEnd();
}

H323Connection::AnswerCallResponse OpenMCUH323Connection::OnAnswerCall(const PString & /*caller*/,
                                                                  const H323SignalPDU & setupPDU,
                                                                  H323SignalPDU & /*connectPDU*/)
{
  requestedRoom = ep.IncomingConferenceRequest(*this, setupPDU);

  if (requestedRoom.IsEmpty())
    return AnswerCallDenied;

  return AnswerCallNow;
}

void OpenMCUH323Connection::OnEstablished()
{
  H323Connection::OnEstablished();
}

class MemberDeleteThread : public PThread
{
  public:
    MemberDeleteThread(OpenMCUH323EndPoint * _ep, Conference * _conf, ConferenceMember * _cm)
      : PThread(10000, AutoDeleteThread), ep(_ep), conf(_conf), cm(_cm)
    {
      Resume();
    }

    void Main()
    {
      cm->WaitForClose();
      PThread::Sleep(1000);
      if (conf->RemoveMember(cm))
        ep->GetConferenceManager().RemoveConference(conf->GetID());
      PThread::Sleep(1000);
      delete cm;
    }

  protected:
    OpenMCUH323EndPoint * ep;
    Conference * conf;
    ConferenceMember * cm;
};

void OpenMCUH323Connection::JoinConference(const PString & roomToJoin)
{
  PWaitAndSignal m(connMutex);

  if (conference != NULL)
    return;

  BOOL joinSuccess = FALSE;

  if (!roomToJoin.IsEmpty()) {
    // create or join the conference
    ConferenceManager & manager = ((OpenMCUH323EndPoint &)ep).GetConferenceManager();
    Conference * newConf = manager.MakeAndLockConference(roomToJoin);
    if (newConf != NULL) {
      conference = newConf;
      conferenceIdentifier = conference->GetID();

      if(videoTransmitCodec!=NULL)
       videoTransmitCodec->encoderCacheKey = ((long)conference&0xFFFFFF00)|(videoTransmitCodec->encoderCacheKey&0x000000FF);
      conferenceMember = new H323Connection_ConferenceMember(conference, ep, GetCallToken(), this, isMCU);

      if (!conferenceMember->IsJoined())
        PTRACE(1, "MCU\tMember connection refused");
      else
        joinSuccess = TRUE;

      manager.UnlockConference();

      if(!joinSuccess) {
        new MemberDeleteThread(&ep, conference, conferenceMember);
        conferenceMember = NULL;
        conference = NULL;
      }
    }
  }

  if(!joinSuccess)
    ChangeWelcomeState(JoinFailed);
}

void OpenMCUH323Connection::LeaveConference()
{
  PWaitAndSignal m(connMutex);

  if (conference != NULL && conferenceMember != NULL) {
    LogCall();

    new MemberDeleteThread(&ep, conference, conferenceMember);
    conferenceMember = NULL;
    conference = NULL;

    // - called from another thread than usual
    // - may clear the call immediately
    ChangeWelcomeState(ConferenceEnded);
  }
}

BOOL OpenMCUH323Connection::OpenAudioChannel(BOOL isEncoding, unsigned /* bufferSize */, H323AudioCodec & codec)
{
  PWaitAndSignal m(connMutex);

  PString codecName = codec.GetMediaFormat();

  codec.SetSilenceDetectionMode( H323AudioCodec::NoSilenceDetection );

  if (!isEncoding) {
    audioReceiveCodecName = codecName;
    codec.AttachChannel(new IncomingAudio(ep, *this, codec.GetSampleRate()), TRUE);
  } else {
    audioTransmitCodecName = codecName;
    codec.AttachChannel(new OutgoingAudio(ep, *this, codec.GetSampleRate()), TRUE);
  }

  return TRUE;
}

void OpenMCUH323Connection::OpenVideoCache(H323VideoCodec & srcCodec)
{
 Conference *conf = conference;
     //  const H323Capabilities &caps = ep.GetCapabilities();
 if(conf == NULL) 
 { // creating conference if needed
  OpenMCUH323EndPoint & ep = OpenMCU::Current().GetEndpoint();
  ConferenceManager & manager = ((OpenMCUH323EndPoint &)ep).GetConferenceManager();
  conf = manager.MakeAndLockConference(requestedRoom);
  manager.UnlockConference();
 }
// starting new cache thread
  new ConferenceFileMember(conf, srcCodec.GetMediaFormat(), PFile::WriteOnly); 
}

#if OPENMCU_VIDEO
BOOL OpenMCUH323Connection::OpenVideoChannel(BOOL isEncoding, H323VideoCodec & codec)
{
  PWaitAndSignal m(connMutex);

  if (isEncoding) {
    videoTransmitCodec = &codec;
    videoTransmitCodecName = codec.GetMediaFormat();

    PVideoChannel * channel = new PVideoChannel;
    videoGrabber = new PVideoInputDevice_OpenMCU(*this);
    if (videoGrabber == NULL) {
      PTRACE(3, "Cannot create MCU video input driver");
      return FALSE;
    }

//    int fr=ep.GetVideoFrameRate();
    unsigned fr=ep.GetVideoFrameRate();
    if(fr > codec.GetTargetFrameRate()) fr=codec.GetTargetFrameRate();
    else codec.SetTargetFrameTimeMs(1000/fr);

    codec.formatString+=(PString)fr + "_" + requestedRoom;
    cout << codec.formatString << "\n";

    videoTransmitCodecName = codec.formatString; // override previous definition

    if(GetRemoteApplication().Find("PCS-") != P_MAX_INDEX && codec.formatString.Find("H.264") != P_MAX_INDEX) 
       codec.cacheMode = 3;

    if(!codec.cacheMode) 
    {
     if(!codec.CheckCacheRTP()) 
     {
      OpenVideoCache(codec);
      while(!codec.CheckCacheRTP()) { PThread::Sleep(100); }
     }
     codec.AttachCacheRTP();
    }

    if (!InitGrabber(videoGrabber, codec.GetWidth(), codec.GetHeight(), fr)) {
      delete videoGrabber;
      videoGrabber = NULL;
      return FALSE;
    }

    codec.SetTxQualityLevel(ep.GetVideoTxQuality());

    videoGrabber->Start();
    channel->AttachVideoReader(videoGrabber);

    if (!codec.AttachChannel(channel,TRUE))
      return FALSE;

  } else {


    videoReceiveCodec = &codec;
    if(conference && conference->IsModerated() == "+") conference->FreezeVideo(this);
//    codec.OnFreezeVideo(TRUE);

    videoReceiveCodecName = codec.GetMediaFormat();

    videoDisplay = new PVideoOutputDevice_OpenMCU(*this);

    if (!videoDisplay->Open("")) {
      delete videoDisplay;
      return FALSE;
    }

    videoDisplay->SetFrameSize(codec.GetWidth(), codec.GetHeight()); // needed to enable resize
    videoDisplay->SetColourFormatConverter("YUV420P");

    PVideoChannel * channel = new PVideoChannel; 
    channel->AttachVideoPlayer(videoDisplay); 
    if (!codec.AttachChannel(channel,TRUE))
      return FALSE;
  }
 
  return TRUE;
}

void OpenMCUH323Connection::OnClosedLogicalChannel(const H323Channel & channel)
{
 H323Codec * codec = channel.GetCodec();
 if(codec == videoTransmitCodec) videoTransmitCodec = NULL; 
 if(codec == videoReceiveCodec) videoReceiveCodec = NULL;
}

void OpenMCUH323Connection::RestartGrabber() { videoGrabber->Restart(); }

BOOL OpenMCUH323Connection::InitGrabber(PVideoInputDevice * grabber, int newFrameWidth, int newFrameHeight, int newFrameRate)
{
  PTRACE(4, "Video grabber set to " << newFrameWidth << "x" << newFrameHeight);

  //if (!(pfdColourFormat.IsEmpty()))
  //  grabber->SetPreferredColourFormat(pfdColourFormat);

  if (!grabber->Open("", FALSE)) {
    PTRACE(3, "Failed to open the video input device");
    return FALSE;
  }

  //if (!grabber->SetChannel(ep.GetVideoPlayMode())) {
  //  PTRACE(3, "Failed to set channel to " << ep.GetVideoPlayMode());
  //  return FALSE;
  //}

  //if (!grabber->SetVideoFormat(
  //    ep.GetVideoIsPal() ? PVideoDevice::PAL : PVideoDevice::NTSC)) {
  //  PTRACE(3, "Failed to set format to " << (ep.GetVideoIsPal() ? "PAL" : "NTSC"));
  //  return FALSE;
  //}

  if (!grabber->SetColourFormatConverter("YUV420P") ) {
    PTRACE(3,"Failed to set format to yuv420p");
    return FALSE;
  }


  if (newFrameRate != 0) {
    if (!grabber->SetFrameRate(newFrameRate)) {
      PTRACE(3, "Failed to set framerate to " << newFrameRate);
      return FALSE;
    }
  }

  if (!grabber->SetFrameSizeConverter(newFrameWidth,newFrameHeight,FALSE)) {
    PTRACE(3, "Failed to set frame size to " << newFrameWidth << "x" << newFrameHeight);
    return FALSE;
  }

  return TRUE;
}

#endif

void OpenMCUH323Connection::OnUserInputString(const PString & str)
{
  PWaitAndSignal m(connMutex);

  if (conferenceMember != NULL)
    conferenceMember->SendUserInputIndication(str);
}


BOOL OpenMCUH323Connection::OnIncomingAudio(const void * buffer, PINDEX amount)
{
  PWaitAndSignal m(connMutex);

  // If record file is open, write data to it
  if (recordFile.IsOpen()) {
    recordFile.Write(buffer, amount);

    recordDuration += amount / 2;
    if (recordDuration > recordLimit) {
      recordFile.Close();
      OnFinishRecording();
    }
    else {
      const WORD * samples = (const WORD *)buffer;
      PINDEX sampleCount = amount / 2;
      BOOL silence = TRUE;
      while (sampleCount-- > 0 && silence) {
        if (*samples > 100 || *samples < -100)
          silence = FALSE;
        ++samples;
      }
      if (!silence)
        recordSilenceCount = 0;
      else {
        recordSilenceCount += amount / 2;
        if ((recordSilenceThreshold > 0) && (recordSilenceCount >= recordSilenceThreshold)) {
          recordFile.Close();
          OnFinishRecording();
        }
      }
    }
  }

  else if (conferenceMember != NULL)
    conferenceMember->WriteAudio(buffer, amount);

  return TRUE;
}

void OpenMCUH323Connection::StartRecording(const PFilePath & filename, unsigned limit, unsigned threshold)
{
  if (!recordFile.Open(filename, PFile::ReadWrite, PFile::Create | PFile::Truncate))
    return;

  recordSilenceCount = 0;
  recordDuration = 0;

  recordSilenceThreshold = threshold * 8000;
  recordLimit            = limit * 8000;
}

void OpenMCUH323Connection::OnFinishRecording()
{
}

BOOL OpenMCUH323Connection::OnOutgoingAudio(void * buffer, PINDEX amount)
{
  // When the prodedure begins, play the welcome file
  if (welcomeState == NotStartedYet) {
    ChangeWelcomeState(PlayingWelcome);
  }

  for (;;) {
    // Do actions that are not triggered by events
    OnWelcomeProcessing();

    // If a wave is not playing, we may continue now
    if (!playFile.IsOpen())
      break;

    // Wait for wave file completion
    if (playFile.Read(buffer, amount)) {
      int len = playFile.GetLastReadCount();
      if (len < amount) {
        memset(((BYTE *)buffer)+len, 0, amount-len);
      }
      //playDelay.Delay(amount/16);

      // Exit now since the buffer is ready
      return TRUE;
    }

    PTRACE(4, "MCU\tFinished playing file");
    playFile.Close();

    // Wave completed, if no event should be fired
    //  then we may continue now
    if(!wavePlayingInSameState)
      break;

    // Fire wave completion event
    OnWelcomeWaveEnded();

    // We should repeat the loop now because the callback
    //  above might have started a new wave file
  }

  PWaitAndSignal m(connMutex);

  // If a we are connected to a conference and no wave
  //  is playing, read data from the conference
  if (conferenceMember != NULL) {
    conferenceMember->ReadAudio(buffer, amount);
    return TRUE;
  }

  // Generate silence
  return FALSE;
}

///////////////////////////////////////////////////////////////////////////////////////

void OpenMCUH323Connection::ChangeWelcomeState(int newState)
{
  PWaitAndSignal m(connMutex);

  if(welcomeState != newState)
  {
    PTRACE(4, "MCU\tEntering welcome state " << newState);
    welcomeState = newState;
    wavePlayingInSameState = FALSE;
    OnWelcomeStateChanged();
  }
}

void OpenMCUH323Connection::PlayWelcomeFile(BOOL useTheFile, PFilePath & fileToPlay)
{
  playFile.Close();

  wavePlayingInSameState = TRUE;

  if(useTheFile) {
    if(playFile.Open(fileToPlay, PFile::ReadOnly))
    {
      PTRACE(4, "MCU\tPlaying welcome procedure file " << fileToPlay);
      return;
    }
    else
      PTRACE(3, "MCU\tFailed to play welcome procedure file " << fileToPlay);
  }

  // File not played, call the wave end callback anyway
  OnWelcomeWaveEnded();
}

void OpenMCUH323Connection::OnWelcomeStateChanged()
{
  PFilePath fn;

//  OpenMCU & mcu = OpenMCU::Current();

  switch(welcomeState) {

    case PlayingWelcome:
      // Welcome file not implemented yet
      PlayWelcomeFile(FALSE, fn);
      break;

    case PlayingConnecting:
      PlayWelcomeFile(FALSE, fn);
//      PlayWelcomeFile(mcu.GetConnectingWAVFile(fn), fn);
      break;

    case CompleteConnection:
      JoinConference(requestedRoom);
      break;

    case JoinFailed:
    case ConferenceEnded:
      // Goodbye file not implemented yet
      PlayWelcomeFile(FALSE, fn);
      break;

    default:
      // Do nothing
      break;
  }
}

void OpenMCUH323Connection::OnWelcomeProcessing()
{
}

void OpenMCUH323Connection::OnWelcomeWaveEnded()
{
  switch(welcomeState) {

    case PlayingWelcome:
      ChangeWelcomeState(PlayingConnecting);
      break;

    case PlayingConnecting:
      ChangeWelcomeState(CompleteConnection);
      break;

    case JoinFailed:
    case ConferenceEnded:
      ClearCall();
      break;

    default:
      // Do nothing
      break;
  }
}

///////////////////////////////////////////////////////////////////////////////////////

#if OPENMCU_VIDEO

//
// this function is called whenever a connection needs a frame of video for output
//

BOOL OpenMCUH323Connection::OnOutgoingVideo(void * buffer, int width, int height, PINDEX & amount)
{
  PWaitAndSignal m(connMutex);

  if (conferenceMember != NULL)
    conferenceMember->ReadVideo(buffer, width, height, amount);
  else return FALSE;
/*  
  else if (!GetPreMediaFrame(buffer, width, height, amount)) {
    if ((width == CIF4_WIDTH) && (height == CIF4_HEIGHT))
      MCUVideoMixer::FillCIF4YUVFrame(buffer, 0, 0, 0);
    else if ((width == CIF_WIDTH) && (height == CIF_HEIGHT))
      MCUVideoMixer::FillCIFYUVFrame(buffer, 0, 0, 0);
    else if ((width == QCIF_WIDTH) && (height == QCIF_HEIGHT))
      MCUVideoMixer::FillQCIFYUVFrame(buffer, 0, 0, 0);
  }
*/
  return TRUE;
}

BOOL OpenMCUH323Connection::GetPreMediaFrame(void * buffer, int width, int height, PINDEX & amount)
{
  return OpenMCU::Current().GetPreMediaFrame(buffer, width, height, amount);
}

//
// this function is called whenever a connection receives a frame of video
//

BOOL OpenMCUH323Connection::OnIncomingVideo(const void * buffer, int width, int height, PINDEX amount)
{
  if (conferenceMember != NULL)
    conferenceMember->WriteVideo(buffer, width, height, amount);
  return TRUE;
}

#endif // OPENMCU_VIDEO


///////////////////////////////////////////////////////////////

H323Connection_ConferenceMember::H323Connection_ConferenceMember(Conference * _conference, OpenMCUH323EndPoint & _ep, const PString & _h323Token, ConferenceMemberId _id, BOOL _isMCU)
  : ConferenceMember(_conference, _id, _isMCU), ep(_ep), h323Token(_h323Token)
{ 
  conference->AddMember(this);
}

H323Connection_ConferenceMember::~H323Connection_ConferenceMember()
{
  PTRACE(4, "H323Connection_ConferenceMember deleted");
}

void H323Connection_ConferenceMember::Close()
{
  OpenMCUH323Connection * conn = (OpenMCUH323Connection *)ep.FindConnectionWithLock(h323Token);
  if (conn != NULL) {
    conn->LeaveConference();
    conn->Unlock();
  }
}
/*
PString H323Connection_ConferenceMember::GetTitle() const
{
  PString output;
  OpenMCUH323Connection * conn = (OpenMCUH323Connection *)ep.FindConnectionWithLock(h323Token);
  if (conn != NULL) {
    output = conn->GetRemoteName();
    conn->Unlock();
  }
  return output;
}
*/
PString H323Connection_ConferenceMember::GetTitle() const
{
  PString output;
  if(id!=this)
  {
   OpenMCUH323Connection * conn = (OpenMCUH323Connection *)ep.FindConnectionWithLock(h323Token);
   if(conn == NULL) return h323Token;
   if(conn->GetConferenceMember() == this || conn->GetConferenceMember() == NULL) 
   {
    output = conn->GetRemoteName(); 
   }
   else PTRACE(1, "MCU\tWrong connection in GetTitle for " << h323Token);
   conn->Unlock();
  }
  return output;
}

PString H323Connection_ConferenceMember::GetMonitorInfo(const PString & hdr)
{ 
  PStringStream output;
  OpenMCUH323Connection * conn = (OpenMCUH323Connection *)ep.FindConnectionWithLock(h323Token);
  if (conn != NULL) {
    output << hdr << "Remote Address: " << conn->GetRemotePartyAddress() << "\n"
           << hdr << "AudioCodecs: " << conn->GetAudioTransmitCodecName() << '/' << conn->GetAudioReceiveCodecName() << "\n"
#if OPENMCU_VIDEO
           << hdr << "VideoCodecs: " << conn->GetVideoTransmitCodecName() << '/' << conn->GetVideoReceiveCodecName() << "\n"
#endif           
           ;
    conn->Unlock();
  }
  return output;
}

void H323Connection_ConferenceMember::SetName()
{
 if(id!=this)
 {
  cout << "SetName " << h323Token << "\n";
  int connLock = 0;
  OpenMCUH323Connection * conn = (OpenMCUH323Connection *)ep.FindConnectionWithLock(h323Token);
  if(conn == NULL)
  {
   conn = (OpenMCUH323Connection *)ep.FindConnectionWithoutLock(h323Token);
   if(conn == NULL) return;
//   conn = (OpenMCUH323Connection *)ep.FindConnectionWithLock(h323Token);
//   PTRACE(1, "MCU\tDeadlock in SetName for " << h323Token);
  }
  else connLock = 1;
  H323Connection_ConferenceMember * connConferenceMemeber = conn->GetConferenceMember();
  if( connConferenceMemeber == this || connConferenceMemeber == NULL) 
  {
   name = conn->GetRemotePartyName();
   const char *nam = name; 
   if(strstr(nam,"[")!=NULL) { if(connLock != 0) conn->Unlock(); return; } // incoming call
   // outgoing call
   PString sname = conn->GetRemotePartyAddress();
   nam = sname; nam = strstr(nam,"$"); if(nam==NULL) nam = sname; else nam++;
   char buf[128]; sscanf(nam,"%127[^:@]",buf);
   sname = buf;
   name += " ["+sname+"]";
  }
  else PTRACE(1, "MCU\tWrong connection in SetName for " << h323Token);
  if(connLock != 0) conn->Unlock();
 }
}

// signal to codec plugin for disable(enable) decoding incoming video from unvisible(visible) member
void H323Connection_ConferenceMember::SetFreezeVideo(BOOL disable) const
{
 if(id!=this)
 {
  cout << "SetFreezeVideo\n";
  OpenMCUH323Connection * conn = (OpenMCUH323Connection *)ep.FindConnectionWithLock(h323Token);
  if(conn == NULL) return;
  H323Connection_ConferenceMember * connConferenceMemeber = conn->GetConferenceMember();
  if( connConferenceMemeber == this || connConferenceMemeber == NULL) 
  {
   H323VideoCodec *codec = conn->GetVideoReceiveCodec();
   if(codec) codec->OnFreezeVideo(disable);
  }
  else PTRACE(1, "MCU\tWrong connection in SetFreezeVideo for " << h323Token);
  conn->Unlock();
 }
}

void H323Connection_ConferenceMember::SendUserInputIndication(const PString & str)
{ 
  PTRACE(3, "Conference\tConnection " << id << " sending user indication " << str);
  int lockcount = 3;
  OpenMCUH323Connection * conn = (OpenMCUH323Connection *)ep.FindConnectionWithLock(h323Token);
  while(conn == NULL && lockcount > 0)
  {
   conn = (OpenMCUH323Connection *)ep.FindConnectionWithoutLock(h323Token);
   if(conn == NULL) return;
   conn = (OpenMCUH323Connection *)ep.FindConnectionWithLock(h323Token);
   PTRACE(1, "MCU\tDeadlock in SendUserInputIndication for " << h323Token);
   lockcount--;
  }
  if(conn == NULL) return;

  PStringStream msg; PStringStream utfmsg; if(conn->GetRemoteApplication().Find("MyPhone")!=P_MAX_INDEX){
    static const int table[128] = { // cp1251 -> utf8 translation based on http://www.linux.org.ru/forum/development/3968525
      0x82D0,0x83D0,  0x9A80E2,0x93D1,  0x9E80E2,0xA680E2,0xA080E2,0xA180E2,0xAC82E2,0xB080E2,0x89D0,0xB980E2,0x8AD0,0x8CD0,0x8BD0,0x8FD0,
      0x92D1,0x9880E2,0x9980E2,0x9C80E2,0x9D80E2,0xA280E2,0x9380E2,0x9480E2,0,       0xA284E2,0x99D1,0xBA80E2,0x9AD1,0x9CD1,0x9BD1,0x9FD1,
      0xA0C2,0x8ED0,  0x9ED1,  0x88D0,  0xA4C2,  0x90D2,  0xA6C2,  0xA7C2,  0x81D0,  0xA9C2,  0x84D0,0xABC2,  0xACC2,0xADC2,0xAEC2,0x87D0,
      0xB0C2,0xB1C2,  0x86D0,  0x96D1,  0x91D2,  0xB5C2,  0xB6C2,  0xB7C2,  0x91D1,  0x9684E2,0x94D1,0xBBC2,  0x98D1,0x85D0,0x95D1,0x97D1,
      0x90D0,0x91D0,  0x92D0,  0x93D0,  0x94D0,  0x95D0,  0x96D0,  0x97D0,  0x98D0,  0x99D0,  0x9AD0,0x9BD0,  0x9CD0,0x9DD0,0x9ED0,0x9FD0,
      0xA0D0,0xA1D0,  0xA2D0,  0xA3D0,  0xA4D0,  0xA5D0,  0xA6D0,  0xA7D0,  0xA8D0,  0xA9D0,  0xAAD0,0xABD0,  0xACD0,0xADD0,0xAED0,0xAFD0,
      0xB0D0,0xB1D0,  0xB2D0,  0xB3D0,  0xB4D0,  0xB5D0,  0xB6D0,  0xB7D0,  0xB8D0,  0xB9D0,  0xBAD0,0xBBD0,  0xBCD0,0xBDD0,0xBED0,0xBFD0,
      0x80D1,0x81D1,  0x82D1,  0x83D1,  0x84D1,  0x85D1,  0x86D1,  0x87D1,  0x88D1,  0x89D1,  0x8AD1,0x8BD1,  0x8CD1,0x8DD1,0x8ED1,0x8FD1
    };
    for(PINDEX i=0;i<str.GetLength();i++){
      unsigned int charcode=(BYTE)str[i];
      if(charcode&128){
        if((charcode=table[charcode&127])){
          utfmsg << (char)charcode << (char)(charcode >> 8);
          if(charcode >>= 16) utfmsg << (char)charcode;
        }
      } else utfmsg << (char)charcode;
    }
  } else utfmsg << str;
  msg << "<font color=blue><b>" << name << "</b>: " << utfmsg << "</font>"; OpenMCU::Current().HttpWriteEvent(msg);

  if (conn->GetConferenceMember() == this || conn->GetConferenceMember() == NULL) 
  {
   PString msg = "[" + conn->GetRemotePartyName() + "]: " + str;
   if (lock.Wait()) 
   {
    MemberListType::iterator r;
    for (r = memberList.begin(); r != memberList.end(); ++r)
      if (r->second != NULL) r->second->OnReceivedUserInputIndication(msg);
    lock.Signal();
   }
  }
  else PTRACE(1, "MCU\tWrong connection in SendUserInputIndication for " << h323Token);
  conn->Unlock();
}

///////////////////////////////////////////////////////////////

void OpenMCUH323Connection::LogCall(const BOOL accepted)
{
  if(!controlChannel && !signallingChannel) return;
  H323TransportAddress address = GetControlChannel().GetRemoteAddress();
  PIPSocket::Address ip;
  WORD port;
  PStringStream stringStream, timeStream;
  address.GetIpAndPort(ip, port);
  timeStream << GetConnectionStartTime().AsString("hh:mm:ss");
  stringStream << ' ' << "caller-ip:" << ip << ':' << port << ' '
	             << GetRemotePartyName() 
               << " room:" << ((conference != NULL) ? conference->GetNumber() : PString());

  if (accepted) {
    PStringStream connectionDuration;
    connectionDuration << setprecision(0) << setw(5) << (PTime() - GetConnectionStartTime());
    OpenMCU::Current().LogMessage(timeStream + stringStream	+ " connection duration:" + connectionDuration);
  }
  else 
    OpenMCU::Current().LogMessage(timeStream + " Call denied:" + stringStream);		
}

///////////////////////////////////////////////////////////////

OutgoingAudio::OutgoingAudio(H323EndPoint & _ep, OpenMCUH323Connection & _conn, unsigned int _sampleRate)
  : ep(_ep), conn(_conn)
{
  os_handle = 0;
#if USE_SWRESAMPLE
  swrc = NULL;
#else
  swrc = 0;
#endif
  sampleRate = _sampleRate;
  if(sampleRate == 0) sampleRate = 8000; // g711
  if(sampleRate != 16000)
  {
#if USE_SWRESAMPLE
   swrc = swr_alloc_set_opts(NULL, AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_S16, sampleRate,
                            AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_S16, 16000,
                            0, NULL);
   swr_init(swrc);
#else
   swrc = 1;
#endif //USE_SWRESAMPLE
  }
}

void OutgoingAudio::CreateSilence(void * buffer, PINDEX amount)
{
  memset(buffer, 0, amount);
//  lastReadCount = amount;
}

BOOL OutgoingAudio::Read(void * buffer, PINDEX amount)
{
  PINDEX amount16 = amount;
  void * buffer16 = buffer;

//  cout << "SEND amount=" << amount << "\n";
  
  PWaitAndSignal mutexR(audioChanMutex);
  
  if (!IsOpen())
    return FALSE;

#if USE_SWRESAMPLE
  if(swrc != NULL) //8000
#else
  if(swrc) //8000
#endif
  {
   amount16 = amount*16000/sampleRate; if((amount16&1) != 0) amount16+=1;
   if((amount16>>1) > swr_buf.GetSize()) swr_buf.SetSize(amount16>>1);
   buffer16 = swr_buf.GetPointer();
  }

  // do the read call here, by calling conn.OnOutgoingAudio():
  if (!conn.OnOutgoingAudio(buffer16, amount16))
    CreateSilence(buffer16, amount16);

#if USE_SWRESAMPLE
  if(swrc != NULL) //8000
   swr_convert(swrc, (uint8_t **)&buffer, amount>>1, (const uint8_t **)&buffer16, amount16>>1);
#else
  if(swrc) //8000
    for(PINDEX i=0;i<(amount>>1);i++) ((short*)buffer)[i] = ((short*)buffer16)[i*16000/sampleRate];
#endif

  delay.Delay(amount16 / 32);

  lastReadCount = amount;

  return TRUE;
}

BOOL OutgoingAudio::Close()
{
  if (!IsOpen()) 
    return FALSE;

  PWaitAndSignal mutexC(audioChanMutex);
  os_handle = -1;
#if USE_SWRESAMPLE
  if(swrc != NULL) swr_free(&swrc);
#else
  swrc = 0;
#endif

  return TRUE;
}

///////////////////////////////////////////////////////////////////////////

IncomingAudio::IncomingAudio(H323EndPoint & _ep, OpenMCUH323Connection & _conn, unsigned int _sampleRate)
  : ep(_ep), conn(_conn)
{
  os_handle = 0;
#if USE_SWRESAMPLE
  swrc = NULL;
#else
  swrc = 0;
#endif
  sampleRate = _sampleRate;
  if(sampleRate == 0) sampleRate = 8000; // g711
#if USE_SWRESAMPLE
  if(sampleRate != 16000)
  {
   swrc = swr_alloc_set_opts(NULL, AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_S16, 16000,
                            AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_S16, sampleRate,
                            0, NULL);
   swr_init(swrc);
  }
#else
  swrc = 1;
#endif
}

BOOL IncomingAudio::Write(const void * buffer, PINDEX amount)
{
  PINDEX amount16 = amount;

//  cout << "RECV amount=" << amount << "\n";

  PWaitAndSignal mutexW(audioChanMutex);
  
  if (!IsOpen())
    return FALSE;

#if USE_SWRESAMPLE
  if(swrc != NULL) //8000
#else
  if(swrc) //8000
#endif
  {
   void * buffer16;
   amount16 = amount*16000/sampleRate; if((amount16&1) != 0) amount16+=1;
   if((amount16>>1) > swr_buf.GetSize()) swr_buf.SetSize(amount16>>1);
   buffer16 = swr_buf.GetPointer();
#if USE_SWRESAMPLE
   swr_convert(swrc, (uint8_t **)&buffer16, amount16>>1, (const uint8_t **)&buffer, amount>>1);
#else
   for(PINDEX i=0;i<(amount16>>1);i++) ((short*)buffer16)[i] = ((short*)buffer)[i*sampleRate/16000];
#endif
   conn.OnIncomingAudio(buffer16, amount16);
  }
  else conn.OnIncomingAudio(buffer, amount);

  delay.Delay(amount16 / 32);

  return TRUE;
}

BOOL IncomingAudio::Close()
{
  if (!IsOpen())
    return FALSE;

  PWaitAndSignal mutexA(audioChanMutex);
  os_handle = -1;
#if USE_SWRESAMPLE
  if(swrc != NULL) swr_free(&swrc);
#else
  swrc = 0;
#endif

  return TRUE;
}

///////////////////////////////////////////////////////////////////////////

