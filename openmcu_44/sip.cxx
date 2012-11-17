#include <ptlib.h>
#include "mcu.h"
#include <stdio.h>
#include <string.h>

RTP_UDP *OpenMCUSipConnection::CreateRTPSession(int pt, SipCapability *sc)
{
  int id = (!sc->media)?RTP_Session::DefaultAudioSessionID:RTP_Session::DefaultVideoSessionID;
  RTP_UDP * session = (RTP_UDP *)(rtpSessions.UseSession(id));
  if(session == NULL)
  {
   session = new RTP_UDP(
#ifdef H323_RTP_AGGREGATE
                useRTPAggregation ? endpoint.GetRTPAggregator() : NULL,
#endif    
                id, remoteIsNAT);
   rtpSessions.AddSession(session);
   PIPSocket::Address lIP(localIP); 
   PIPSocket::Address rIP(remoteIP);
   session->Open(lIP,5000,10000,endpoint.GetRtpIpTypeofService(),*this,NULL,NULL);
   session->SetRemoteSocketInfo(rIP,sc->port,TRUE);
   sc->lport = session->GetLocalDataPort();
   sdp_msg = sdp_msg + "m=" + ((!sc->media)?"audio ":"video ")
           + PString(sc->lport) + " RTP/AVP " + PString(pt) + "\r\n";
   if(sc->bandwidth) sdp_msg = sdp_msg + "b=AS:" + PString(sc->bandwidth) + "\r\n";
   sdp_msg = sdp_msg + "a=sendrecv\r\n";
   if(pt != 0 && pt != 8)
   {
    sdp_msg = sdp_msg + "a=rtpmap:" + PString(pt) + " " + sc->format + "/" + PString(sc->clock);
    if(sc->cnum) sdp_msg = sdp_msg + "/" + PString(sc->cnum);
    sdp_msg = sdp_msg + "\r\n";
    if(!sc->parm.IsEmpty())
     sdp_msg = sdp_msg + "a=fmtp:" + PString(pt) + " " + sc->parm + "\r\n";
   }
  }
  return session;
}

int OpenMCUSipConnection::CreateAudioChannel(int pt, int dir)
{
 SipCapMapType::iterator cir = sipCaps.find(pt);
 PString h323Name = cir->second->h323;
 H323Capability * cap = cir->second->cap;
 if(cap!=NULL)
 {
  RTP_UDP *session = CreateRTPSession(pt, cir->second);
  H323_RTPChannel *channel = 
     new H323_RTPChannel(*this, *cap, (!dir)?H323Channel::IsReceiver:H323Channel::IsTransmitter, *session);
  if (pt >= RTP_DataFrame::DynamicBase && pt <= RTP_DataFrame::MaxPayloadType)  
   channel->SetDynamicRTPPayloadType(pt);
  if(!dir) cir->second->inpChan = channel; else cir->second->outChan = channel;
 }
 return 0;
}

int OpenMCUSipConnection::CreateVideoChannel(int pt, int dir)
{
 SipCapMapType::iterator cir = sipCaps.find(pt);
 PString h323Name = cir->second->h323;
 H323Capability * cap = cir->second->cap;
 if(cap!=NULL)
 {
  RTP_UDP *session = CreateRTPSession(pt, cir->second);
  H323_RTPChannel *channel = 
     new H323_RTPChannel(*this, *cap, (!dir)?H323Channel::IsReceiver:H323Channel::IsTransmitter, *session);
  if (pt >= RTP_DataFrame::DynamicBase && pt <= RTP_DataFrame::MaxPayloadType)  
   channel->SetDynamicRTPPayloadType(pt);
  if(!dir) cir->second->inpChan = channel; else cir->second->outChan = channel;
 }
 return 0;
}

void OpenMCUSipConnection::CreateLogicalChannels()
{
 if(scap >= 0) // audio capability is set
 {
  CreateAudioChannel(scap,0);
  CreateAudioChannel(scap,1);
 }
 if(vcap >= 0) // video capability is set
 {
  CreateVideoChannel(vcap,0);
  CreateVideoChannel(vcap,1);
 }
}

void OpenMCUSipConnection::StartChannel(int pt, int dir)
{
 if(pt<0) return;
 SipCapMapType::iterator cir = sipCaps.find(pt);
 if(dir == 0 && cir->second->inpChan) cir->second->inpChan->Start();
 if(dir == 1 && cir->second->outChan) cir->second->outChan->Start();
}

void OpenMCUSipConnection::StartReceiveChannels()
{
 StartChannel(scap,0);
 StartChannel(vcap,0);
}

void OpenMCUSipConnection::StartTransmitChannels()
{
 StartChannel(scap,1);
 StartChannel(vcap,1);
}

void OpenMCUSipConnection::StopChannel(int pt, int dir)
{
 if(pt<0) return;
 SipCapMapType::iterator cir = sipCaps.find(pt);
 if(dir==0 && cir->second->inpChan) cir->second->inpChan->CleanUpOnTermination();
 if(dir==1 && cir->second->outChan) cir->second->outChan->CleanUpOnTermination();
}

void OpenMCUSipConnection::StopTransmitChannels()
{
 StopChannel(scap,1);
 StopChannel(vcap,1);
}

void OpenMCUSipConnection::StopReceiveChannels()
{
 StopChannel(scap,0);
 StopChannel(vcap,0);
}

void OpenMCUSipConnection::DeleteMediaChannels(int pt)
{
 if(pt<0) return;
 SipCapMapType::iterator cir = sipCaps.find(pt);
 if(cir->second->inpChan) { delete cir->second->inpChan; cir->second->inpChan = NULL; }
 if(cir->second->outChan) { delete cir->second->outChan; cir->second->outChan = NULL; }
}

void OpenMCUSipConnection::DeleteChannels()
{
 DeleteMediaChannels(scap);
 DeleteMediaChannels(vcap);
}

void OpenMCUSipConnection::CleanUpOnCallEnd()
{
  PTRACE(1, "MCUSIP\tCleanUpOnCallEnd");
  StopTransmitChannels();
  StopReceiveChannels();
  DeleteChannels();
  videoReceiveCodecName = videoTransmitCodecName = "none";
  videoReceiveCodec = NULL;
  videoTransmitCodec = NULL;
}

void SipCapability::Print()
{
 cout << "Payload: " << payload << " Media: " << media << " Direction: " << dir << " Port: " << port << "\r\n";
 cout << "Clock: " << clock << " Bandwidth: " << bandwidth << "\r\n";
 cout << "Format: " << format << "\r\n";
 cout << "Parameters: " << parm << "\r\n\r\n";
}


void OpenMCUSipConnection::FindCapability_H263(SipCapability &c,PStringArray &keys, const char * _H323Name, const char * _SIPName)
{
 PString H323Name(_H323Name);
 PString SIPName(_SIPName);
 for(int kn=0; kn<keys.GetSize(); kn++) 
 { 
  if(keys[kn].Find(SIPName + "=")==0)
   { 
    c.cap = H323Capability::Create(H323Name);
    if(c.cap == NULL) return;
    vcap = c.payload; c.h323 = H323Name; c.parm += keys[kn]; 
    OpalMediaFormat & wf = c.cap->GetWritableMediaFormat(); 
    int mpi = (keys[kn].Mid(SIPName.GetLength()+1)).AsInteger();
    cout << "mpi " << mpi << "\n";
    wf.SetOptionInteger(SIPName + " MPI",mpi);
    return; 
   } 
  }
}

void OpenMCUSipConnection::SelectCapability_H263(SipCapability &c,PStringArray &tvCaps)
{
 int f=0; // annex f
 PStringArray keys = c.parm.Tokenise(";");
 c.parm = "";
 for(int kn=0; kn<keys.GetSize(); kn++) 
  { if(keys[kn] == "F=1") { c.parm = "F=1;"; f=1; break; } }
 
 if(tvCaps.GetStringsIndex("H.263-16CIF{sw}")!=P_MAX_INDEX && c.cap == NULL)
  FindCapability_H263(c,keys,"H.263-16CIF{sw}","CIF16");
 if(tvCaps.GetStringsIndex("H.263-4CIF{sw}")!=P_MAX_INDEX && c.cap == NULL)
  FindCapability_H263(c,keys,"H.263-4CIF{sw}","CIF4");
 if(tvCaps.GetStringsIndex("H.263-CIF{sw}")!=P_MAX_INDEX && c.cap == NULL)
  FindCapability_H263(c,keys,"H.263-CIF{sw}","CIF");
 if(tvCaps.GetStringsIndex("H.263-QCIF{sw}")!=P_MAX_INDEX && c.cap == NULL)
  FindCapability_H263(c,keys,"H.263-QCIF{sw}","QCIF");
 if(tvCaps.GetStringsIndex("H.263-SQCIF{sw}")!=P_MAX_INDEX && c.cap == NULL)
  FindCapability_H263(c,keys,"H.263-SQCIF{sw}","SQCIF");

 if(c.cap)
 {
  OpalMediaFormat & wf = c.cap->GetWritableMediaFormat();
  wf.SetOptionBoolean("_advancedPrediction",f);
  if(c.bandwidth) wf.SetOptionInteger("Max Bit Rate",c.bandwidth*1000);
  else if(bandwidth) wf.SetOptionInteger("Max Bit Rate",bandwidth*1000);
 }
}


void OpenMCUSipConnection::SelectCapability_H263p(SipCapability &c,PStringArray &tvCaps)
{
 int f=0,d=0,e=0,g=0; // annexes
 PStringArray keys = c.parm.Tokenise(";");
 c.parm = "";
 for(int kn=0; kn<keys.GetSize(); kn++) 
 { 
  if(keys[kn] == "F=1") { c.parm += "F=1;"; f=1; } 
  else if(keys[kn] == "D=1") { c.parm += "D=1;"; d=1; } 
  else if(keys[kn] == "E=1") { c.parm += "E=1;"; e=1; } 
  else if(keys[kn] == "G=1") { c.parm += "G=1;"; g=1; } 
 }
 
 if(tvCaps.GetStringsIndex("H.263p-16CIF{sw}")!=P_MAX_INDEX && c.cap == NULL)
  FindCapability_H263(c,keys,"H.263p-16CIF{sw}","CIF16");
 if(tvCaps.GetStringsIndex("H.263p-4CIF{sw}")!=P_MAX_INDEX && c.cap == NULL)
  FindCapability_H263(c,keys,"H.263p-4CIF{sw}","CIF4");
 if(tvCaps.GetStringsIndex("H.263p-CIF{sw}")!=P_MAX_INDEX && c.cap == NULL)
  FindCapability_H263(c,keys,"H.263p-CIF{sw}","CIF");
 if(tvCaps.GetStringsIndex("H.263p-QCIF{sw}")!=P_MAX_INDEX && c.cap == NULL)
  FindCapability_H263(c,keys,"H.263p-QCIF{sw}","QCIF");
 if(tvCaps.GetStringsIndex("H.263p-SQCIF{sw}")!=P_MAX_INDEX && c.cap == NULL)
  FindCapability_H263(c,keys,"H.263p-SQCIF{sw}","SQCIF");

 if(c.cap)
 {
  OpalMediaFormat & wf = c.cap->GetWritableMediaFormat();
  wf.SetOptionBoolean("_advancedPrediction",f);
  wf.SetOptionBoolean("_unrestrictedVector",d);
  wf.SetOptionBoolean("_arithmeticCoding",e);
  wf.SetOptionBoolean("_pbFrames",g);
  if(c.bandwidth) wf.SetOptionInteger("Max Bit Rate",c.bandwidth*1000);
  else if(bandwidth) wf.SetOptionInteger("Max Bit Rate",bandwidth*1000);
 }
}

/*
packetization-mode=1;profile-level-id=42C01E 
*/

const struct h241_to_x264_level {
    int h241;
    int idc;
} h241_to_x264_levels[]=
{
    { 15, 9 },
    { 19,10 },
    { 22,11 },
    { 29,12 },
    { 36,13 },
    { 43,20 },
    { 50,21 },
    { 57,22 },
    { 64,30 },
    { 71,31 },
    { 78,32 },
    { 85,40 },
    { 92,41 },
    { 99,42 },
    { 106,50},
    { 113,51},
    { 0 }
};

void OpenMCUSipConnection::SelectCapability_H264(SipCapability &c,PStringArray &tvCaps)
{
 int profile = 0, level = 0;
 int max_mbps = 0, max_fs = 0, max_br = 0;
 PStringArray keys = c.parm.Tokenise(";");
 for(int kn = 0; kn < keys.GetSize(); kn++) 
 { 
  if(keys[kn].Find("profile-level-id=") == 0) 
  { 
   int p = (keys[kn].Tokenise("=")[1]).AsInteger(16);
   profile = (p>>16); level = (p&255);
  } 
  else if(keys[kn].Find("max-mbps=") == 0) 
  { 
   max_mbps = (keys[kn].Tokenise("=")[1]).AsInteger();
  } 
  else if(keys[kn].Find("max-fs=") == 0) 
  { 
   max_fs = (keys[kn].Tokenise("=")[1]).AsInteger();
  } 
  else if(keys[kn].Find("max-br=") == 0) 
  { 
   max_br = (keys[kn].Tokenise("=")[1]).AsInteger();
  } 
 }
 cout << "profile " << profile << " level " << level << "\n";
 if(profile == 0 || level == 0) return;

 int l = 0;
 while(h241_to_x264_levels[l].idc != 0)
 {
  if(level == h241_to_x264_levels[l].idc) { level = h241_to_x264_levels[l].h241; break; }
  l++;
 }
 profile = 64;

 cout << "profile " << profile << " level " << level << "\n";
 int cl = 0;
 for(int cn = 0; cn < tvCaps.GetSize(); cn++)
 {
  if(tvCaps[cn].Find("H.264")==0)
  {
   H323Capability *cap = H323Capability::Create(tvCaps[cn]);
   if(cap != NULL)
   {
    const OpalMediaFormat & mf = cap->GetMediaFormat(); 
    int flevel = mf.GetOptionInteger("Generic Parameter 42");
    cout << "flevel" << flevel << "\n";
    if(flevel > cl && flevel <= level) 
     { cl = flevel; if(c.cap) delete c.cap; c.cap = cap; c.h323 = tvCaps[cn]; }
    else { delete cap; }
    if(flevel == level) break; 
   }
  }
 }

 if(c.cap)
 {
  OpalMediaFormat & wf = c.cap->GetWritableMediaFormat();
  if(c.bandwidth) wf.SetOptionInteger("Max Bit Rate",c.bandwidth*1000);
  else if(bandwidth) wf.SetOptionInteger("Max Bit Rate",bandwidth*1000);
  wf.SetOptionInteger("Generic Parameter 42",level);
  vcap = c.payload;
  wf.SetOptionInteger("Generic Parameter 3",max_mbps);
  wf.SetOptionInteger("Generic Parameter 4",max_fs);
  wf.SetOptionInteger("Generic Parameter 6",max_br);
 }
}


int OpenMCUSipConnection::ProcessInviteEvent(sip_t *sip)
{
 PString request = sip->sip_request->rq_method_name;
 sdp_s = sip->sip_payload->pl_data;
 PStringArray sdp_sa = sdp_s.Lines();
 PIntArray &par = sipCapsId; // full payloads list
 int par_len = 0;
 SipCapMapType &caps = sipCaps;
 
 localIP = sip->sip_to->a_url->url_host;
 if(sip->sip_to->a_url->url_user && sip->sip_to->a_url->url_user[0]!=0)
  requestedRoom = sip->sip_to->a_url->url_user;
 remotePartyAddress = sip->sip_from->a_url->url_host;
 remotePartyName = sip->sip_from->a_url->url_user;
 remoteName = remotePartyName;
 if(sip->sip_user_agent && sip->sip_user_agent->g_string)
  remoteApplication = sip->sip_user_agent->g_string;
 callToken = remotePartyName + "@" + remotePartyAddress + ":" + PString(sip->sip_call_id->i_id);

 cout << "Name: " << remotePartyName << " Addr: " << remotePartyAddress << "\n";

 int port = -1, media = -1, def_dir = 3, dir = 3, def_bw = 0, bw = 0;
 for(int line=0; line<sdp_sa.GetSize(); line++)
 {
  char tag = sdp_sa[line][0];
  PStringArray words = sdp_sa[line].Tokenise(" ",FALSE);

  if(tag =='o')
  {
   if(words.GetSize() < 6) continue;
   remoteIP = words[5];
  }
  else if(tag == 'm')
  {
   for(int cn=0; cn<par.GetSize(); cn++)
   {
    SipCapMapType::iterator cir = caps.find(par[cn]);
    if(cir != caps.end()) continue; // payload found, here is nothing to do
    SipCapability *c = new SipCapability(par[cn],media,dir,port,bw);
    caps.insert(SipCapMapType::value_type(par[cn],c));
   }
   port = -1; media = -1; bw = 0; dir = def_dir; // reset media level values to default
   if(words.GetSize() < 4) continue; // empty fmt list
   if(words[2] != "RTP/AVP") continue; // non rtp media is not supported
   if(words[0].Find("audio")!=P_MAX_INDEX) media = 0;
   else if(words[0].Find("video")!=P_MAX_INDEX) media = 1;
   else media = 2;
   port = words[1].AsInteger();
   for(int wn = 3; wn < words.GetSize(); wn++) { par.SetAt(par_len,words[wn].AsInteger()); par_len++; }
  }
  else if(tag == 'a')
  {
   PString atag = words[0].Mid(2);
   if(atag == "recvonly")      { if(media < 0) def_dir = 1; dir = 1; }
   else if(atag == "sendonly") { if(media < 0) def_dir = 2; dir = 2; }
   else if(atag == "sendrecv") { if(media < 0) def_dir = 3; dir = 3; }
   else if(atag == "inactive") { if(media < 0) def_dir = 0; dir = 0; }
   else if(atag.Find("rtpmap")!=P_MAX_INDEX)
   {
    int payload;
    if(words.GetSize() < 2) continue; // invalid rtpmap string
    PStringArray tokens = atag.Tokenise(":");
    if(tokens.GetSize() < 2) continue; // invalid rtpmap string
    payload = tokens[1].AsInteger();
    tokens = words[1].Tokenise("/");
    if(tokens.GetSize() < 2) continue; // invalid rtpmap string
    SipCapability *c = new SipCapability(payload,media,dir,port,bw);
    c->format = tokens[0];
    c->clock = tokens[1].AsInteger();
    if(tokens.GetSize() == 3) c->cnum = tokens[2].AsInteger();
    caps.insert(SipCapMapType::value_type(payload,c));
   }
   else if(atag.Find("fmtp")!=P_MAX_INDEX)
   {
    int payload;
    if(words.GetSize() < 2) continue; // invalid fmtp string
    PStringArray tokens = atag.Tokenise(":");
    if(tokens.GetSize() < 2) continue; // invalid fmtp string
    payload = tokens[1].AsInteger();
    SipCapMapType::iterator cir = caps.find(payload);
    if(cir == caps.end()) continue; // payload is not exist
    cir->second->parm += words[1] + ";";
   }
  }
  else if(tag == 'b')
  {
   PStringArray tokens = words[0].Tokenise(":");
   if(tokens.GetSize() < 2) continue; // invalid bandwidth string
   if(tokens[0] == "b=AS") bw = tokens[1].AsInteger();
   if(tokens[0] == "b=TIAS") bw = tokens[1].AsInteger()/1000;
   if(media == -1) { bandwidth = bw; bw = 0; } // connection level value
  }
  cout << "line: " + sdp_sa[line] + "\r\n";
 } 

 for(int cn=0; cn<par.GetSize(); cn++)
 {
  SipCapMapType::iterator cir = caps.find(par[cn]);
  if(cir != caps.end()) { cir->second->Print(); continue; } // payload found, here is nothing to do
  SipCapability *c = new SipCapability(par[cn],media,dir,port,bw);
  caps.insert(SipCapMapType::value_type(par[cn],c));
  c->Print();
 }

 PStringArray tsCaps, tvCaps;
 int cn = 0; while(endpoint.tsCaps[cn]!=NULL) { tsCaps.AppendString(endpoint.tsCaps[cn]); cn++; }
 cn = 0; while(endpoint.tvCaps[cn]!=NULL) { tvCaps.AppendString(endpoint.tvCaps[cn]); cn++; }

 cout << tsCaps << "\n";
 cout << tvCaps << "\n";

 scap = -1; vcap = -1;
 for(int cn=0; cn<par.GetSize() && (scap < 0 || vcap < 0); cn++)
 {
  SipCapMapType::iterator cir = caps.find(par[cn]);
  SipCapability &c = cir->second[0];
  cout << c.format << "\n";
  if(c.media == 0)
  {
   if(scap >= 0) continue;
   if(c.payload == 0 &&
      tsCaps.GetStringsIndex("G.711-uLaw-64k")!=P_MAX_INDEX) //PCMU
    { scap = 0; c.h323 = "G.711-uLaw-64k{sw}"; c.cap = H323Capability::Create("G.711-uLaw-64k{sw}"); }
   else if(c.payload == 8 &&
      tsCaps.GetStringsIndex("G.711-ALaw-64k")!=P_MAX_INDEX) //PCMA
    { scap = 8; c.h323 = "G.711-ALaw-64k{sw}"; c.cap = H323Capability::Create("G.711-ALaw-64k{sw}"); }
  }
  else if(c.media == 1)
  {
   if(vcap >= 0) continue;
   if(c.format.ToLower() == "h263") SelectCapability_H263(c,tvCaps);
   else if(c.format.ToLower() == "h263-1998") SelectCapability_H263p(c,tvCaps);
   else if(c.format.ToLower() == "h264") SelectCapability_H264(c,tvCaps);
  }
 }
 cout << scap << " " << vcap << "\r\n";
 sdp_msg = "v=0\r\no=";
 sdp_msg = sdp_msg + requestedRoom + " ";
 sdp_seq++;
 sdp_msg = sdp_msg + PString(sdp_id) + " ";
 sdp_msg = sdp_msg + PString(sdp_seq);
 sdp_msg = sdp_msg + " IN IP4 ";
 sdp_msg = sdp_msg + localIP + "\r\n";
 sdp_msg = sdp_msg + "s=openmcu\r\n";
 sdp_msg = sdp_msg + "c=IN IP4 ";
 sdp_msg = sdp_msg + localIP + "\r\n";
 if(bandwidth) sdp_msg = sdp_msg + "b=AS:" + PString(bandwidth) + "\r\n";
 sdp_msg = sdp_msg + "t=0 0\r\n";
 CreateLogicalChannels();
 ep.OnIncomingSipConnection(callToken,*this);
 JoinConference(requestedRoom);
 if(conferenceMember == NULL || conference == NULL) return 0;
 return 1;
}

void OpenMCUSipConnection::SipReply200(nta_agent_t *agent, msg_t *msg)
{
  PString contact = "<sip:" + requestedRoom + "@" + localIP + ":5060>";

  PTRACE(1, "MCUSIP\tSending SIP 200 OK to " << contact << " msg " << sdp_msg);

  if(sdp_msg.IsEmpty())
  {
    nta_msg_treply(agent,msg, SIP_200_OK, SIPTAG_CONTACT_STR((const char*)contact),TAG_END());
    return;
  }
  char *sdp_txt = strdup(sdp_msg);
  msg_common_t ms = {0, 0, sip_payload_class, sdp_txt, strlen(sdp_txt)};
  sip_payload_t sdp = {{ms}, NULL, sdp_txt, strlen(sdp_txt)};
  nta_msg_treply(agent,msg, SIP_200_OK, SIPTAG_CONTACT_STR((const char*)contact),
                 SIPTAG_CONTENT_TYPE_STR("application/sdp"), SIPTAG_PAYLOAD(&sdp), TAG_END());
  free(sdp_txt);
  StartReceiveChannels();
  StartTransmitChannels();
}

int OpenMCUSipEndPoint::ProcessSipEvent_cb(nta_agent_t *agent,
                       msg_t *msg,
                       sip_t *sip)
{
 SipKey sik;
 sik.addr = inet_addr(sip->sip_via->v_host);
 sik.port = atoi(sip->sip_via->v_port);
 sik.sid = sip->sip_call_id->i_id;

 PString request = sip->sip_request->rq_method_name;
 
 size_t sip_msg_len = 0;
 char * sip_msg = msg_as_string(&home, msg, NULL, 0, &sip_msg_len);
 
 PTRACE(1, "MCUSIP\tReceived SIP message: \n" << sip_msg);
 cout << request << "\n";

 if(request == "INVITE")
 {
  if(sip->sip_payload==NULL) return 0;
  if(sip->sip_payload->pl_data==NULL) return 0;

  PTRACE(1, "MCUSIP\tReceived SIP SDP\n" << sip->sip_payload->pl_data);

  SipConnectionMapType::iterator scr = sipConnMap.find(sik);
  if(scr != sipConnMap.end()) return 0; // connection already exist, ignoring invite

  PTRACE(1, "MCUSIP\tNew SIP INVITE");

  OpenMCUSipConnection *sCon = new OpenMCUSipConnection(this, ep);
  if(!sCon->ProcessInviteEvent(sip)) 
  {
   return 0; // here we can sen nothing or 486
  }
  sCon->SipReply200(agent, msg); // send ok and start logical channels
  sipConnMap.insert(SipConnectionMapType::value_type(sik,sCon));
  return 0;
 }
 if(request == "BYE")
 {
  SipConnectionMapType::iterator scr = sipConnMap.find(sik);
  if(scr == sipConnMap.end()) return 0;

  PTRACE(1, "MCUSIP\tNew SIP BYE");
  OpenMCUSipConnection *sCon = scr->second;
  sipConnMap.erase(sik);
  sCon->sdp_msg.MakeEmpty();
  sCon->SipReply200(agent, msg);
  sCon->LeaveConference(); // leave conference and delete connection
  return 0;
 }
 if(request == "OPTIONS")
 {
  PString contact = "<sip:openmcu@" + PString(sip->sip_to->a_url[0].url_host) + ":5060>";
  nta_msg_treply(agent,msg, SIP_200_OK, SIPTAG_CONTACT_STR((const char*)contact),TAG_END());
  return 0;
 }
 return 0;
}

void OpenMCUSipEndPoint::MainLoop()
{
 SipConnectionMapType::iterator scr;
 while(1)
 {
  if(terminating) return;
  for (scr = sipConnMap.begin(); scr != sipConnMap.end(); scr++) 
  {
   OpenMCUSipConnection *sCon = scr->second;
   RTP_Session * as = sCon->GetSession(RTP_Session::DefaultAudioSessionID);
   RTP_Session * vs = sCon->GetSession(RTP_Session::DefaultVideoSessionID);
   int count = 0;
   if(as) count += as->GetPacketsReceived() + as->GetRtpcReceived();
   if(vs) count += vs->GetPacketsReceived() + vs->GetRtpcReceived();
   if(count == sCon->inpBytes) sCon->noInpTimeout++;
   else { sCon->noInpTimeout = 0; sCon->inpBytes = count; }
   if(sCon->noInpTimeout == 30) // 15 sec timeout
   {
    PTRACE(1, "MCUSIP\t15 sec timeout waiting incoming stream data");
    sipConnMap.erase(scr->first);
    sCon->StopTransmitChannels();
    sCon->StopReceiveChannels();
    sCon->DeleteChannels();
    sCon->LeaveConference(); // leave conference and delete connection
    break;
   }
  }
  su_root_sleep(root,500);
 }
}

void OpenMCUSipEndPoint::Main()
{
 su_init();
 su_home_init(&home);
 root = su_root_create(NULL);

 if(root == NULL) return;
 agent = nta_agent_create(root, NULL, ProcessSipEventWrap_cb, (nta_agent_magic_t *)this, TAG_NULL());
 if(agent != NULL)
 {
  MainLoop();
  nta_agent_destroy(agent);
 }
 
 su_root_destroy(root);
 root = NULL;
 su_home_deinit(&home);
 su_deinit();
}
