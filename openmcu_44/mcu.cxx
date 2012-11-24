
#include <ptlib.h>

#include "version.h"
#include "mcu.h"
#include "h323.h"

#if USE_LIBJPEG
extern "C" {
#include <jpeglib.h>
}
#endif

const WORD DefaultHTTPPort = 1420;

extern PHTTPServiceProcess::Info ProductInfo;

static const char LogLevelKey[]           = "Log Level";
static const char TraceLevelKey[]         = "Trace level";
static const char UserNameKey[]           = "Username";
static const char PasswordKey[]           = "Password";
static const char HttpPortKey[]           = "HTTP Port";
static const char HttpLinkEventBufferKey[]= "Room control event buffer size";

static const char CallLogFilenameKey[]    = "Call log filename";

#if P_SSL
static const char HTTPCertificateFileKey[]  = "HTTP Certificate";
#endif
static const char DefaultRoomKey[]          = "Default room";
static const char DefaultRoomTimeLimitKey[] = "Room time limit";

static const char DefaultCallLogFilename[] = "mcu_log.txt"; 
static const char DefaultRoom[]            = "room101";
static const char CreateEmptyRoomKey[]     = "Auto create empty room";
static const char AllowLoopbackCallsKey[]  = "Allow loopback during bulk invite";

#if OPENMCU_VIDEO
static const char ForceSplitVideoKey[]   = "Force split screen video";
#endif

#define new PNEW


///////////////////////////////////////////////////////////////

class MyPConfigPage : public PConfigPage
{
 public:
   MyPConfigPage(PHTTPServiceProcess & app,const PString & title, const PString & section, const PHTTPAuthority & auth)
    : PConfigPage(app,title,section,auth){};
   void SetString(PString str){string=str;};
   PString GetString(){return string;};
 private:
};

#if USE_LIBJPEG
class JpegFrameHTTP : public PServiceHTTPString
{
  public:
    JpegFrameHTTP(OpenMCU & app, PHTTPAuthority & auth);
    BOOL OnGET (PHTTPServer & server, const PURL &url, const PMIMEInfo & info, const PHTTPConnectionInfo & connectInfo);
    PMutex mutex;
  private:
    OpenMCU & app;
};
#endif

class InteractiveHTTP : public PServiceHTTPString
{
  public:
    InteractiveHTTP(OpenMCU & app, PHTTPAuthority & auth);
    BOOL OnGET (PHTTPServer & server, const PURL &url, const PMIMEInfo & info, const PHTTPConnectionInfo & connectInfo);
  private:
    OpenMCU & app;
};

class MainStatusPage : public PServiceHTTPString
{
 // PCLASSINFO(MainStatusPage, PServiceHTTPString);

  public:
    MainStatusPage(OpenMCU & app, PHTTPAuthority & auth);
    
    virtual BOOL Post(
      PHTTPRequest & request,
      const PStringToString &,
      PHTML & msg
    );
  
  private:
    OpenMCU & app;
};

class InvitePage : public PServiceHTTPString
{
  public:
    InvitePage(OpenMCU & app, PHTTPAuthority & auth);

    virtual BOOL Post(
      PHTTPRequest & request,       // Information on this request.
      const PStringToString & data, // Variables in the POST data.
      PHTML & replyMessage          // Reply message for post.
    );
  
  private:
    OpenMCU & app;
};

class SelectRoomPage : public PServiceHTTPString
{
  public:
    SelectRoomPage(OpenMCU & app, PHTTPAuthority & auth);
    
    virtual BOOL Post(
      PHTTPRequest & request,
      const PStringToString &,
      PHTML & msg
    );
  
  private:
    OpenMCU & app;
};




///////////////////////////////////////////////////////////////
// This really isn't the default count only a counter
// for sending aliases and prefixes to the gatekeeper
int OpenMCU::defaultRoomCount = 5;

VideoMixConfigurator OpenMCU::vmcfg;

OpenMCU::OpenMCU()
  : OpenMCUProcessAncestor(ProductInfo)
{
  endpoint = NULL;
  sipendpoint = NULL;
}

void OpenMCU::Main()
{
  Suspend();
}

BOOL OpenMCU::OnStart()
{
  // change to the default directory to the one containing the executable
  PDirectory exeDir = GetFile().GetDirectory();

#if defined(_WIN32) && defined(_DEBUG)
  // Special check to aid in using DevStudio for debugging.
  if (exeDir.Find("\\Debug\\") != P_MAX_INDEX)
    exeDir = exeDir.GetParent();
#endif
  exeDir.Change();

  httpNameSpace.AddResource(new PHTTPDirectory("data", "data"));
  httpNameSpace.AddResource(new PServiceHTTPDirectory("html", "html"));

  manager  = CreateConferenceManager();
  endpoint = CreateEndPoint(*manager);
  sipendpoint = new OpenMCUSipEndPoint(endpoint);

  return PHTTPServiceProcess::OnStart();
}

void OpenMCU::OnStop()
{
  sipendpoint->terminating = 1;
  sipendpoint->WaitForTermination(10000);
  delete sipendpoint;
  sipendpoint = NULL;

  delete endpoint;
  endpoint = NULL;

  delete manager;
  manager = NULL;

  PHTTPServiceProcess::OnStop();
}

void OpenMCU::OnControl()
{
  // This function get called when the Control menu item is selected in the
  // tray icon mode of the service.
  PStringStream url;
  url << "http://";

  PString host = PIPSocket::GetHostName();
  PIPSocket::Address addr;
  if (PIPSocket::GetHostAddress(host, addr))
    url << host;
  else
    url << "localhost";

  url << ':' << DefaultHTTPPort;

  PURL::OpenBrowser(url);
}

BOOL OpenMCU::Initialise(const char * initMsg)
{
  PConfig cfg("Parameters");

  // Set log level as early as possible
//  SetLogLevel((PSystemLog::Level)cfg.GetInteger(LogLevelKey, GetLogLevel()));
#if PTRACING

//    SetLogLevel(PSystemLog::Debug6);
//    PTrace::Initialise(6,"trace.txt");
    int TraceLevel=cfg.GetInteger(TraceLevelKey, 6);
    SetLogLevel((PSystemLog::Level)TraceLevel);
    PTrace::Initialise(TraceLevel,"trace.txt");

/*  if (GetLogLevel() >= PSystemLog::Warning)
    PTrace::SetLevel(GetLogLevel()-PSystemLog::Warning);
  else
    PTrace::SetLevel(0);
  PTrace::ClearOptions(PTrace::Timestamp);
  PTrace::SetOptions(PTrace::DateAndTime); */
#endif

  // Get the HTTP basic authentication info
  PString adminUserName = cfg.GetString(UserNameKey);
  PString adminPassword = PHTTPPasswordField::Decrypt(cfg.GetString(PasswordKey));

  PHTTPSimpleAuth authority(GetName(), adminUserName, adminPassword);

  // Create the parameters URL page, and start adding fields to it
  MyPConfigPage * rsrc = new MyPConfigPage(*this, "Parameters", "Parameters", authority);

  // HTTP authentication username/password
  rsrc->Add(new PHTTPStringField(UserNameKey, 25, adminUserName));
  rsrc->Add(new PHTTPPasswordField(PasswordKey, 25, adminPassword));

//rsrc->Add(new PHTTPStringField("<h3>Other</h3><!--", 1, "","<td></td><td></td>"));

  // Log level for messages
  rsrc->Add(new PHTTPIntegerField(LogLevelKey,
                                  PSystemLog::Fatal, PSystemLog::NumLogLevels-1,
                                  GetLogLevel(),
                                  "1=Fatal only, 2=Errors, 3=Warnings, 4=Info, 5=Debug"));

  // default log file name
  logFilename = cfg.GetString(CallLogFilenameKey, DefaultCallLogFilename);
  rsrc->Add(new PHTTPStringField(CallLogFilenameKey, 50, logFilename));

  // Trace level
  rsrc->Add(new PHTTPIntegerField(TraceLevelKey, 0, 6, TraceLevel, "0...6"));

  // Buffered events
  httpBuffer=cfg.GetInteger(HttpLinkEventBufferKey, 100);
  httpBufferedEvents.SetSize(httpBuffer);
  rsrc->Add(new PHTTPIntegerField(HttpLinkEventBufferKey, 10, 1000, httpBuffer, "10...1000"));
  httpBufferIndex=0; httpBufferComplete=0;

#if P_SSL
  // SSL certificate file.
  PString certificateFile = cfg.GetString(HTTPCertificateFileKey, "server.pem");
  rsrc->Add(new PHTTPStringField(HTTPCertificateFileKey, 25, certificateFile));
  if (!SetServerCertificate(certificateFile, TRUE)) {
    PSYSTEMLOG(Fatal, "MCU\tCould not load certificate \"" << certificateFile << '"');
    return FALSE;
  }
#endif

  // HTTP Port number to use.
  WORD httpPort = (WORD)cfg.GetInteger(HttpPortKey, DefaultHTTPPort);
  rsrc->Add(new PHTTPIntegerField(HttpPortKey, 1, 32767, httpPort));

  endpoint->Initialise(cfg, rsrc);
  if(endpoint->behind_masq){PStringStream msq; msq<<"Masquerading as "<<*(endpoint->masqAddressPtr); HttpWriteEvent(msq);}

#if OPENMCU_VIDEO
  forceScreenSplit = cfg.GetBoolean(ForceSplitVideoKey, TRUE);
  rsrc->Add(new PHTTPBooleanField(ForceSplitVideoKey, forceScreenSplit));
#if USE_LIBYUV
  scaleFilter=libyuv::LIBYUV_FILTER;
#endif
#endif

  // get default "room" (conference) name
  defaultRoomName = cfg.GetString(DefaultRoomKey, DefaultRoom);
  rsrc->Add(new PHTTPStringField(DefaultRoomKey, 25, defaultRoomName));

  // create/don't create empty room with default name at start:
  rsrc->Add(new PHTTPBooleanField(CreateEmptyRoomKey, FALSE));

  // get conference time limit 
  roomTimeLimit = cfg.GetInteger(DefaultRoomTimeLimitKey, 0);
  rsrc->Add(new PHTTPIntegerField(DefaultRoomTimeLimitKey, 0, 10800, roomTimeLimit));

  OnCreateConfigPage(cfg, *rsrc);

  // allow/disallow self-invite:
  allowLoopbackCalls = cfg.GetBoolean(AllowLoopbackCallsKey, FALSE);
  rsrc->Add(new PHTTPBooleanField(AllowLoopbackCallsKey, allowLoopbackCalls));

  // Finished the resource to add, generate HTML for it and add to name space
  PServiceHTML html("System Parameters");
  rsrc->BuildHTML(html);
  httpNameSpace.AddResource(rsrc, PHTTPSpace::Overwrite);
  PStringStream html0; BeginPage(html0,"Parameters","Parameters","$PARAMETERS$");
  PString html1 = rsrc->GetString();
  PStringStream html2; EndPage(html2,GetCopyrightText());
  PStringStream htmlpage; htmlpage << html0 << html1 << html2;

// Sections http://openmcu.ru/forum/index.php?topic=287.msg2375#msg2375
// Looks like they cause problems with video decoding by MCU time to time

//  htmlpage.Replace("right>Username<TD","left colspan=3><h3>Authentication</h3></tr><tr><td align=RIGHT>Username<TD");
//  htmlpage.Replace("right>Enable video<TD","left colspan=3><h3>Video</h3></tr><tr><td align=RIGHT>Enable video<TD");
////  htmlpage.Replace("right>Connecting WAV File<TD","left colspan=3><h3>Sound [DISABLED]</h3></tr><tr><td align=RIGHT>Connecting WAV File<TD");
//  htmlpage.Replace("right>HTTP Port<TD","left colspan=3><h3>Network</h3></tr><tr><td align=RIGHT>HTTP Port<TD");
//  htmlpage.Replace("right>Log Level<TD","left colspan=3><h3>Logs</h3></tr><tr><td align=RIGHT>Log Level<TD");
//  htmlpage.Replace("right>Default room<TD","left colspan=3><h3>Other</h3></tr><tr><td align=RIGHT>Default room<TD");

  rsrc->SetString(htmlpage);

  // Create the status page
  httpNameSpace.AddResource(new MainStatusPage(*this, authority), PHTTPSpace::Overwrite);

  // Create invite conference page
  httpNameSpace.AddResource(new InvitePage(*this, authority), PHTTPSpace::Overwrite);

  // Create room selection page
  httpNameSpace.AddResource(new SelectRoomPage(*this, authority), PHTTPSpace::Overwrite);

#if USE_LIBJPEG
  // Create JPEG frame via HTTP
  httpNameSpace.AddResource(new JpegFrameHTTP(*this, authority), PHTTPSpace::Overwrite);
#endif

  httpNameSpace.AddResource(new InteractiveHTTP(*this, authority), PHTTPSpace::Overwrite);

  // Add log file links
  if (!systemLogFileName && (systemLogFileName != "-")) {
    httpNameSpace.AddResource(new PHTTPFile("logfile.txt", systemLogFileName, authority));
    httpNameSpace.AddResource(new PHTTPTailFile("tail_logfile", systemLogFileName, authority));
  }

  //  create the home page
  static const char welcomeHtml[] = "welcome.html";
  if (PFile::Exists(welcomeHtml))
    httpNameSpace.AddResource(new PServiceHTTPFile(welcomeHtml, TRUE), PHTTPSpace::Overwrite);
  else {
    PHTML html;
    PStringStream shtml;
         BeginPage(shtml,"OpenMCU Home","OpenMCU Home","$WELCOME$");
/*
    if (!systemLogFileName && systemLogFileName != "-")
      html << PHTML::HotLink("logfile.txt") << "Full Log File" << PHTML::HotLink()
           << PHTML::BreakLine()
           << PHTML::HotLink("tail_logfile") << "Tail Log File" << PHTML::HotLink()
           << PHTML::Paragraph();
*/
         EndPage(shtml,GetCopyrightText());
    html = shtml;
    httpNameSpace.AddResource(new PServiceHTTPString("welcome.html", html), PHTTPSpace::Overwrite);
  }

  // create monitoring page
  PString monitorText = "<!--#equival monitorinfo-->"
                        "<!--#equival mcuinfo-->";
  httpNameSpace.AddResource(new PServiceHTTPString("monitor.txt", monitorText, "text/plain", authority), PHTTPSpace::Overwrite);

  // adding gif images for OTF Control:
  httpNameSpace.AddResource(new PHTTPFile("openmcu.ru_mic_on.gif"), PHTTPSpace::Overwrite);
  httpNameSpace.AddResource(new PHTTPFile("openmcu.ru_mic_off.gif"), PHTTPSpace::Overwrite);
  httpNameSpace.AddResource(new PHTTPFile("openmcu.ru_drop_Abdylas_Tynyshov.gif"), PHTTPSpace::Overwrite);
  httpNameSpace.AddResource(new PHTTPFile("openmcu.ru_vad_vad.gif"), PHTTPSpace::Overwrite);
  httpNameSpace.AddResource(new PHTTPFile("openmcu.ru_vad_disable.gif"), PHTTPSpace::Overwrite);
  httpNameSpace.AddResource(new PHTTPFile("openmcu.ru_vad_chosenvan.gif"), PHTTPSpace::Overwrite);
  httpNameSpace.AddResource(new PHTTPFile("openmcu.ru_invite_Everaldo_Coelho.gif"), PHTTPSpace::Overwrite);
  httpNameSpace.AddResource(new PHTTPFile("openmcu.ru_launched_Ypf.gif"), PHTTPSpace::Overwrite);
  httpNameSpace.AddResource(new PHTTPFile("openmcu.ru_remove.gif"), PHTTPSpace::Overwrite);

  // set up the HTTP port for listening & start the first HTTP thread
  if (ListenForHTTP(httpPort))
    PSYSTEMLOG(Info, "Opened master socket for HTTP: " << httpListeningSocket->GetPort());
  else {
    PSYSTEMLOG(Fatal, "Cannot run without HTTP port: " << httpListeningSocket->GetErrorText());
    return FALSE;
  }

  if(cfg.GetBoolean(CreateEmptyRoomKey, FALSE)) GetEndpoint().OutgoingConferenceRequest(defaultRoomName);

  PSYSTEMLOG(Info, "Service " << GetName() << ' ' << initMsg);
  return TRUE;
}

PCREATE_SERVICE_MACRO(mcuinfo,P_EMPTY,P_EMPTY)
{
  return OpenMCU::Current().GetEndpoint().GetMonitorText();
}

void OpenMCU::OnConfigChanged()
{
}

PString OpenMCU::GetNewRoomNumber()
{
  static PAtomicInteger number = 100;
  return PString(PString::Unsigned, ++number);
}

void OpenMCU::LogMessage(const PString & str)
{
  static PMutex logMutex;
  static PTextFile logFile;

  PTime now;
  PString msg = now.AsString("dd/MM/yyyy") & str;
  logMutex.Wait();

  if (!logFile.IsOpen()) {
    logFile.Open(logFilename, PFile::ReadWrite);
    logFile.SetPosition(0, PFile::End);
  }

  logFile.WriteLine(msg);
  logFile.Close();
  logMutex.Signal();
}

ConferenceManager * OpenMCU::CreateConferenceManager()
{
  return new ConferenceManager();
}

OpenMCUH323EndPoint * OpenMCU::CreateEndPoint(ConferenceManager & manager)
{
  return new OpenMCUH323EndPoint(manager);
}

//////////////////////////////////////////////////////////////////////////////////////////////

PCREATE_SERVICE_MACRO_BLOCK(RoomStatus,P_EMPTY,P_EMPTY,block)
{
  return OpenMCU::Current().GetEndpoint().GetRoomStatus(block);
}

MainStatusPage::MainStatusPage(OpenMCU & _app, PHTTPAuthority & auth)
  : PServiceHTTPString("Status", "", "text/html; charset=utf-8", auth),
    app(_app)
{
  PStringStream html;
  

  html << "<meta http-equiv=\"Refresh\" content=\"30\">\n";
  BeginPage(html,"Status Page","Status Page","$STATUS$");
  html << "<p>"
       << "<table class=\"table table-striped table-bordered table-condensed\">"
       << "<tr>"
       << "<th>"
       << "Room Name"
       << "<th>"
       << "Room Members"

       << "<!--#macrostart RoomStatus-->"
         << "<tr>"
         << "<td>"
         << "<!--#status RoomName-->"
         << "<td>"
         << "<!--#status RoomMembers-->"
       << "<!--#macroend RoomStatus-->"

       << "</table>"

       << "<p>";
       
         EndPage(html,app.GetCopyrightText());
  string = html;
}


BOOL MainStatusPage::Post(PHTTPRequest & request,
                          const PStringToString & data,
                          PHTML & msg)
{
  return TRUE;
}


///////////////////////////////////////////////////////////////


#if USE_LIBJPEG

MCUSimpleVideoMixer* jpegMixer;

void jpeg_init_destination(j_compress_ptr cinfo){
  if(jpegMixer->myjpeg.GetSize()<32768)jpegMixer->myjpeg.SetSize(32768);
  cinfo->dest->next_output_byte=&jpegMixer->myjpeg[0];
  cinfo->dest->free_in_buffer=jpegMixer->myjpeg.GetSize();
}

boolean jpeg_empty_output_buffer(j_compress_ptr cinfo){
  PINDEX oldsize=jpegMixer->myjpeg.GetSize();
  jpegMixer->myjpeg.SetSize(oldsize+16384);
  cinfo->dest->next_output_byte = &jpegMixer->myjpeg[oldsize];
  cinfo->dest->free_in_buffer = jpegMixer->myjpeg.GetSize() - oldsize;
  return true;
}

void jpeg_term_destination(j_compress_ptr cinfo){
  jpegMixer->jpegSize=jpegMixer->myjpeg.GetSize() - cinfo->dest->free_in_buffer;
  jpegMixer->jpegTime=time(0);
}

JpegFrameHTTP::JpegFrameHTTP(OpenMCU & _app, PHTTPAuthority & auth)
  : PServiceHTTPString("Jpeg", "", "image/jpeg", auth),
    app(_app)
{
}

BOOL JpegFrameHTTP::OnGET (PHTTPServer & server, const PURL &url, const PMIMEInfo & info, const PHTTPConnectionInfo & connectInfo)
{
  PWaitAndSignal m(mutex);
  PStringStream room; room << url; if(room.Find("Jpeg?room=")!=0)return FALSE;
  room=room.Right(room.GetLength()-10);
  PINDEX amppos;
  if((amppos=room.Find("&"))!=P_MAX_INDEX) room=room.Left(amppos);
  const unsigned long t1=time(0);
  ConferenceListType & conferenceList = app.GetEndpoint().GetConferenceManager().GetConferenceList();
  ConferenceListType::iterator r; for(r = conferenceList.begin(); r != conferenceList.end(); ++r)
  {
    Conference & conference = *(r->second);
    if(conference.GetNumber()==room)
    {
      jpegMixer=(MCUSimpleVideoMixer*)conference.GetVideoMixer();
      if(t1-(jpegMixer->jpegTime)>1)
      {
        const int width=OpenMCU::vmcfg.vmconf[jpegMixer->GetPositionSet()].splitcfg.mockup_width;
        const int height=OpenMCU::vmcfg.vmconf[jpegMixer->GetPositionSet()].splitcfg.mockup_height;
        struct jpeg_compress_struct cinfo; struct jpeg_error_mgr jerr;
        JSAMPROW row_pointer[1]; int row_stride;
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);
        cinfo.image_width = width; cinfo.image_height = height; cinfo.input_components = 3; cinfo.in_color_space = JCS_RGB;
        PINDEX amount=width*height*3/2;
        unsigned char *videoData=new unsigned char[amount];
        conference.ReadMemberVideo((ConferenceMember*)this,(void*)videoData,width,height,amount);
        PColourConverter * converter = PColourConverter::Create("YUV420P", "RGB24", width, height);
        converter->SetDstFrameSize(width, height);
        unsigned char * bitmap = new unsigned char[width*height*3];
        converter->Convert(videoData,bitmap);
        delete converter;
        delete videoData;
        jpeg_set_defaults(&cinfo);
        cinfo.dest = new jpeg_destination_mgr;
        cinfo.dest->init_destination = &jpeg_init_destination;
        cinfo.dest->empty_output_buffer = &jpeg_empty_output_buffer;
        cinfo.dest->term_destination = &jpeg_term_destination;
        jpeg_start_compress(&cinfo,TRUE);
        row_stride = cinfo.image_width * 3;
        while (cinfo.next_scanline < cinfo.image_height) {
          row_pointer[0] = (JSAMPLE *) & bitmap [cinfo.next_scanline * row_stride];
          (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
        }
        jpeg_finish_compress(&cinfo);
        delete bitmap; delete cinfo.dest; cinfo.dest=NULL;
        jpeg_destroy_compress(&cinfo);
        jpegMixer->jpegTime=t1;
      }
      server.Write(jpegMixer->myjpeg,jpegMixer->jpegSize);
    }
  }
  return FALSE;
}
#endif //#if USE_LIBJPEG

InteractiveHTTP::InteractiveHTTP(OpenMCU & _app, PHTTPAuthority & auth)
  : PServiceHTTPString("Comm", "", "text/html; charset=utf-8", auth),
    app(_app)
{
}

BOOL InteractiveHTTP::OnGET (PHTTPServer & server, const PURL &url, const PMIMEInfo & info, const PHTTPConnectionInfo & connectInfo)
{
  PStringStream room; room << url; if(room.Find("Comm?room=")!=0) room=""; else room=room.Right(room.GetLength()-10);
  PStringStream message;
  PTime now;
  int idx=0;
  message << "HTTP/1.1 200 OK\r\n"
    << "Date: " << now.AsString(PTime::RFC1123, PTime::GMT) << "\r\n"
    << "Server: OpenMCU.ru\r\n"
    << "MIME-Version: 1.0\r\n"
    << "Cache-Control: no-cache, must-revalidate\r\n"
    << "Expires: Sat, 26 Jul 1997 05:00:00 GMT\r\n"
    << "Content-Type: text/html;charset=utf-8\r\n"
    << "Connection: Close\r\n"
    << "\r\n";
  server.Write((const char*)message,message.GetLength());
  server.flush();
  message="<html><body style='font-size:9px;font-family:Verdana,Arial;padding:0px;margin:1px;color:#000'>\r\n";
  message << OpenMCU::Current().HttpGetEvents(idx,room);
  while(server.Write((const char*)message,message.GetLength())) {
    server.flush();
    int count=0;
    message = OpenMCU::Current().HttpGetEvents(idx,room);
    while (message.GetLength()==0 and count < 20){
      count++;
      PThread::Sleep(100);
      message = OpenMCU::Current().HttpGetEvents(idx,room);
    }
    message << "<script>parent.alive()</script>\r\n";
  }
  return FALSE;
}

InvitePage::InvitePage(OpenMCU & _app, PHTTPAuthority & auth)
  : PServiceHTTPString("Invite", "", "text/html; charset=utf-8", auth),
    app(_app)
{
  PStringStream html;

  BeginPage(html,"Invite User","Invite User","$INVITE$");

  html << "<p>"

    << "<form method=\"POST\" class=\"well form-inline\">"
    << "<input type=\"text\" class=\"input-small\" placeholder=\"" << app.GetDefaultRoomName() << "\" name=\"room\" value=\"" << app.GetDefaultRoomName() << "\"> "
    << "<input type=\"text\" class=\"input-small\" placeholder=\"Address\" name=\"address\"><script language='javascript'><!--\ndocument.forms[0].address.focus(); //--></script>"
    << "&nbsp;&nbsp;<input type=\"submit\" class=\"btn\" value=\"Invite\">"
    << "</form>";

  EndPage(html,app.GetCopyrightText());

  string = html;
}


BOOL InvitePage::Post(PHTTPRequest & request,
                          const PStringToString & data,
                          PHTML & msg)
{
  PString room    = data("room");
  PString address = data("address");
  PStringStream html;

  if (room.IsEmpty() || address.IsEmpty()) {
    BeginPage(html,"Invite Failed","Invite Failed","$INVITE_F$");
    html << "<div class=\"alert alert-error\"><b>Insufficient information to perform INVITE</b></div>";
    EndPage(html,OpenMCU::Current().GetCopyrightText()); msg = html;
    return TRUE;
  }

  OpenMCUH323EndPoint & ep = app.GetEndpoint();
  BOOL created = ep.OutgoingConferenceRequest(room);

  if (!created) {
    BeginPage(html,"Invite Failed","Invite Failed","$INVITE_F$");
    html << "<div class=\"alert\"><b>Cannot create</b> room " << room;
    html << "</div>";
    EndPage(html,OpenMCU::Current().GetCopyrightText()); msg = html;
    return TRUE;
  }

  PString h323Token;
  PString * userData = new PString(room);
  if (ep.MakeCall(address, h323Token, userData) == NULL) {
    BeginPage(html,"Invite Failed","Invite Failed","$INVITE_F$");
    html << "<div class=\"alert\"><b>Cannot create make call to</b> " << address;
    html << "</div>";
    EndPage(html,OpenMCU::Current().GetCopyrightText()); msg = html;
    ep.GetConferenceManager().RemoveConference(room);
    return TRUE;
  }

  BeginPage(html,"Invite Succeeded","Invite Succeeded","$INVITE_S$");
  html << "<div class=\"alert alert-success\">Inviting " << address << " to room " << room;
  html << "</div>";

  html << "<p><h3>Invite another:</h3>"
    << "<form method=\"POST\" class=\"well form-inline\">"
    << "<input type=\"text\" class=\"input-small\" name=\"room\" placeholder=\"" << room << "\" value=\"" << room << "\"> "
    << "<input type=\"text\" class=\"input-small\" name=\"address\" placeholder=\"address\"><script language='javascript'><!--\ndocument.forms[0].address.focus(); //--></script>"
    << "&nbsp;&nbsp;&nbsp;<input type=\"submit\" class=\"btn\" value=\"Invite\">"
    << "</form>";

  EndPage(html,OpenMCU::Current().GetCopyrightText()); msg = html;

  return TRUE;
}

PCREATE_SERVICE_MACRO_BLOCK(RoomList,P_EMPTY,P_EMPTY,block)
{
  return OpenMCU::Current().GetEndpoint().GetRoomList(block);
}

SelectRoomPage::SelectRoomPage(OpenMCU & _app, PHTTPAuthority & auth)
  : PServiceHTTPString("Select", "", "text/html; charset=utf-8", auth),
    app(_app)
{
  PStringStream html;

  BeginPage(html,"Select Room","Select Room","$SELECT$");
  html << "<p>"
       << "<form method=\"POST\" class=\"well form-inline\">"
       << "<!--#macrostart RoomList-->"
         << "<!--#status List-->"
       << "<!--#macroend RoomList-->"
       << "&nbsp;";
  EndPage(html,app.GetCopyrightText());

  string = html;
}

BOOL SelectRoomPage::Post(PHTTPRequest & request,
                          const PStringToString & data,
                          PHTML & msg)
{
  msg << OpenMCU::Current().GetEndpoint().SetRoomParams(data);
  return TRUE;
}




// End of File ///////////////////////////////////////////////////////////////
