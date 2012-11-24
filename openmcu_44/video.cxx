
#include <ptlib.h>

#ifdef _WIN32
#pragma warning(disable:4786)
#endif

#include "config.h"

#if OPENMCU_VIDEO

#include "mcu.h"
#include "h323.h"
#include <ptlib/vconvert.h>

#define MAX_SUBFRAMES   100

#if USE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
FT_Library ft_library;
FT_Face ft_face;
FT_Bool ft_use_kerning;
FT_UInt ft_glyph_index,ft_previous;
BOOL ft_subtitles=FALSE;
// Fake error code mean that we need initialize labels from scratch:
#define FT_INITIAL_ERROR 555
// Number of frames skipped before render label:
#define FT_SKIPFRAMES    1
// Number of getting username attempts:
#define FT_ATTEMPTS      100
// Label's options:
#define FT_P_H_CENTER    0x0001
#define FT_P_V_CENTER    0x0002
#define FT_P_RIGHT       0x0004
#define FT_P_BOTTOM      0x0008
#define FT_P_TRANSPARENT 0x0010
#define FT_P_DISABLED    0x0020
#define FT_P_SUBTITLES   0x0040
#define FT_P_REALTIME    0x0080
int ft_error=FT_INITIAL_ERROR;
#endif // #if USE_FREETYPE

#if USE_LIBYUV
#include <libyuv/scale.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////
//
//  declare a video capture (input) device for use with OpenMCU
//

PVideoInputDevice_OpenMCU::PVideoInputDevice_OpenMCU(OpenMCUH323Connection & _mcuConnection)
  : mcuConnection(_mcuConnection)
{
  SetColourFormat("YUV420P");
  channelNumber = 0; 
  grabCount = 0;
  SetFrameRate(25);
}


BOOL PVideoInputDevice_OpenMCU::Open(const PString & devName, BOOL /*startImmediate*/)
{
  //file.SetWidth(frameWidth);
  //file.SetHeight(frameHeight);

  deviceName = devName;

  return TRUE;    
}


BOOL PVideoInputDevice_OpenMCU::IsOpen() 
{
  return TRUE;
}


BOOL PVideoInputDevice_OpenMCU::Close()
{
  return TRUE;
}


BOOL PVideoInputDevice_OpenMCU::Start()
{
  return TRUE;
}


BOOL PVideoInputDevice_OpenMCU::Stop()
{
  return TRUE;
}

BOOL PVideoInputDevice_OpenMCU::IsCapturing()
{
  return IsOpen();
}


PStringList PVideoInputDevice_OpenMCU::GetInputDeviceNames()
{
  PStringList list;
  list.AppendString("openmcu");
  return list;
}


BOOL PVideoInputDevice_OpenMCU::SetVideoFormat(VideoFormat newFormat)
{
  return PVideoDevice::SetVideoFormat(newFormat);
}


int PVideoInputDevice_OpenMCU::GetNumChannels() 
{
  return 0;
}


BOOL PVideoInputDevice_OpenMCU::SetChannel(int newChannel)
{
  return PVideoDevice::SetChannel(newChannel);
}

BOOL PVideoInputDevice_OpenMCU::SetColourFormat(const PString & newFormat)
{
  if (!(newFormat *= "YUV420P"))
    return FALSE;

  if (!PVideoDevice::SetColourFormat(newFormat))
    return FALSE;

  return SetFrameSize(frameWidth, frameHeight);
}


BOOL PVideoInputDevice_OpenMCU::SetFrameRate(unsigned rate)
{
  if (rate < 1)
    rate = 1;
  else if (rate > 50)
    rate = 50;

  return PVideoDevice::SetFrameRate(rate);
}


BOOL PVideoInputDevice_OpenMCU::GetFrameSizeLimits(unsigned & minWidth,
                                           unsigned & minHeight,
                                           unsigned & maxWidth,
                                           unsigned & maxHeight) 
{
  maxWidth  = 2048;
  maxHeight = 2048;
  minWidth  = QCIF_WIDTH;
  minHeight = QCIF_HEIGHT;

  return TRUE;
}


BOOL PVideoInputDevice_OpenMCU::SetFrameSize(unsigned width, unsigned height)
{
 cout << "SetFrameSize " << width << " " << height << "\n";
  if (!PVideoDevice::SetFrameSize(width, height))
    return FALSE;

  videoFrameSize = CalculateFrameBytes(frameWidth, frameHeight, colourFormat);
  scanLineWidth = videoFrameSize/frameHeight;
  return videoFrameSize > 0;
}


PINDEX PVideoInputDevice_OpenMCU::GetMaxFrameBytes()
{
  return GetMaxFrameBytesConverted(videoFrameSize);
}


BOOL PVideoInputDevice_OpenMCU::GetFrameData(BYTE * buffer, PINDEX * bytesReturned)
{    
  grabDelay.Delay(1000/GetFrameRate());
  return GetFrameDataNoDelay(buffer, bytesReturned);
}

 
BOOL PVideoInputDevice_OpenMCU::GetFrameDataNoDelay(BYTE *destFrame, PINDEX * bytesReturned)
{
  grabCount++;

  if (!mcuConnection.OnOutgoingVideo(destFrame, frameWidth, frameHeight, *bytesReturned))
    return FALSE;

  if (converter != NULL) {
    if (!converter->Convert(destFrame, destFrame, bytesReturned))
      return FALSE;
  }

  if (bytesReturned != NULL)
    *bytesReturned = videoFrameSize;

  return TRUE;
}

///////////////////////////////////////////////////////////////////////////////////////
//
//  declare a video display (output) device for use with OpenMCU
//

PVideoOutputDevice_OpenMCU::PVideoOutputDevice_OpenMCU(OpenMCUH323Connection & _mcuConnection)
  : mcuConnection(_mcuConnection)
{
}


BOOL PVideoOutputDevice_OpenMCU::Open(const PString & _deviceName, BOOL /*startImmediate*/)
{
  deviceName = _deviceName;
  return TRUE;
}

BOOL PVideoOutputDevice_OpenMCU::Close()
{
  return TRUE;
}

BOOL PVideoOutputDevice_OpenMCU::Start()
{
  return TRUE;
}

BOOL PVideoOutputDevice_OpenMCU::Stop()
{
  return TRUE;
}

BOOL PVideoOutputDevice_OpenMCU::IsOpen()
{
  return TRUE;
}


PStringList PVideoOutputDevice_OpenMCU::GetOutputDeviceNames()
{
  PStringList list;
  return list;
}


PINDEX PVideoOutputDevice_OpenMCU::GetMaxFrameBytes()
{
  return GetMaxFrameBytesConverted(CalculateFrameBytes(frameWidth, frameHeight, colourFormat));
}


BOOL PVideoOutputDevice_OpenMCU::SetFrameData(unsigned x, unsigned y,
                                              unsigned width, unsigned height,
                                              const BYTE * data,
                                              BOOL /*endFrame*/)
{
  if (x != 0 || y != 0 || width != frameWidth || height != frameHeight) {
    PTRACE(1, "YUVFile output device only supports full frame writes");
    return FALSE;
  }

  return mcuConnection.OnIncomingVideo(data, width, height, width*height*3/2);
}


BOOL PVideoOutputDevice_OpenMCU::EndFrame()
{
  return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////

VideoFrameStoreList::~VideoFrameStoreList()
{
  while (videoFrameStoreList.begin() != videoFrameStoreList.end()) {
    FrameStore * vf = videoFrameStoreList.begin()->second;
    delete vf;
    videoFrameStoreList.erase(videoFrameStoreList.begin());
  }
}

VideoFrameStoreList::FrameStore & VideoFrameStoreList::AddFrameStore(int width, int height)
{ 
  VideoFrameStoreListMapType::iterator r = videoFrameStoreList.find(WidthHeightToKey(width, height));
  if (r != videoFrameStoreList.end())
    return *(r->second);
  FrameStore * vf = new FrameStore(width, height);
  videoFrameStoreList.insert(VideoFrameStoreListMapType::value_type(WidthHeightToKey(width, height), vf)); 
  return *vf;
}

VideoFrameStoreList::FrameStore & VideoFrameStoreList::GetFrameStore(int width, int height) 
{
  VideoFrameStoreListMapType::iterator r = videoFrameStoreList.find(WidthHeightToKey(width, height));
  if (r != videoFrameStoreList.end())
    return *(r->second);

  FrameStore * vf = new FrameStore(width, height);
  videoFrameStoreList.insert(VideoFrameStoreListMapType::value_type(WidthHeightToKey(width, height), vf)); 
  return *vf;
}

void VideoFrameStoreList::InvalidateExcept(int w, int h)
{
  VideoFrameStoreListMapType::iterator r;
  for (r = videoFrameStoreList.begin(); r != videoFrameStoreList.end(); ++r) {
    unsigned int key = r->first;
    int kw, kh; KeyToWidthHeight(key, kw, kh);
    r->second->valid = (w == kw) && (h == kh);
  }
}

VideoFrameStoreList::FrameStore & VideoFrameStoreList::GetNearestFrameStore(int width, int height, BOOL & found)
{
  // see if exact match, and valid
  VideoFrameStoreListMapType::iterator r = videoFrameStoreList.find(WidthHeightToKey(width, height));
  if ((r != videoFrameStoreList.end()) && r->second->valid) {
    found = TRUE;
    return *(r->second);
  }

  // return the first valid framestore
  for (r = videoFrameStoreList.begin(); r != videoFrameStoreList.end(); ++r) {
    if (r->second->valid) {
      found = TRUE;
      return *(r->second);
    }
  }

  // return not found
  found = FALSE;
  return *(videoFrameStoreList.end()->second);
}

///////////////////////////////////////////////////////////////////////////////////////

static inline int ABS(int v)
{  return (v >= 0) ? v : -v; }

MCUVideoMixer::VideoMixPosition::VideoMixPosition(ConferenceMemberId _id,  int _x, int _y, int _w, int _h)
  : id(_id), xpos(_x), ypos(_y), width(_w), height(_h)
{ 
  status = 0;
  type = 0;
  chosenVan = 0;
  prev = NULL;
  next = NULL;
  label_init = FALSE;
  fc = 0;
}

#if USE_FREETYPE
unsigned MCUSimpleVideoMixer::printsubs_calc(unsigned v, char s[10]){
 int slashpos=-1; char s2[10];
 for(int i=0;i<10;i++){ s2[i]=s[i]; if(s[i]==0) break; if(s[i]=='/') slashpos=i; }
 if (slashpos==-1) return atoi(s);
 s2[slashpos]=0; unsigned mul=atoi(s2);
 for(int i=slashpos+1;i<10;i++) s2[i-slashpos-1]=s[i];
 s2[9-slashpos]=0; unsigned div=atoi(s2);
 if(div>0) return v*mul/div;
 PTRACE(6,"FreeType\tprintsubs_calc out !DIVISION BY ZERO! " << v << "*" << mul << "/" << div);
 return 1;
}

void MCUSimpleVideoMixer::Print_Subtitles(VideoMixPosition & vmp, void * buffer, unsigned int fw, unsigned int fh, unsigned int ft_properties){
  vmp.fc++; if(vmp.fc<FT_SKIPFRAMES) return; // Frame counter
  VMPCfgOptions & vmpcfg = OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n];
  if(!(ft_properties & FT_P_REALTIME)) if(vmp.label_init) { // Pasting from buffer
    if(vmp.label_buffer_fw!=fw || vmp.label_buffer_fh!=fh) vmp.label_init=false; // Checking for changed frame size
    else {
      if(ft_properties & FT_P_TRANSPARENT) {
        MixRectIntoFrameGrayscale(vmp.label_buffer,(BYTE *)buffer,vmp.label_x,vmp.label_y,vmp.label_w,vmp.label_h,fw,fh,1);
        ReplaceUV_Rect((BYTE *)buffer,fw,fh,vmpcfg.label_bgcolor>>8,vmpcfg.label_bgcolor&0xFF,0,vmp.label_y,fw,vmp.label_h);
      }
      if(ft_properties & FT_P_SUBTITLES) MixRectIntoFrameSubsMode(vmp.label_buffer,(BYTE *)buffer,vmp.label_x,vmp.label_y,vmp.label_w,vmp.label_h,fw,fh,0);
      if(!(ft_properties & (FT_P_SUBTITLES + FT_P_TRANSPARENT))) CopyRectIntoFrame(vmp.label_buffer,(BYTE *)buffer,vmp.label_x,vmp.label_y,vmp.label_w,vmp.label_h,fw,fh);
      return;
    }
  }
  if (ft_error==FT_INITIAL_ERROR) { // Initialization
    PTRACE(3,"FreeType\tInitialization"); if ((ft_error = FT_Init_FreeType(&ft_library))) return;
    ft_error = FT_New_Face(ft_library,OpenMCU::vmcfg.fontfile,0,&ft_face);
    if (!ft_error){ ft_use_kerning=FT_HAS_KERNING(ft_face); PTRACE(3,"FreeType\tTruetype font " << OpenMCU::vmcfg.fontfile << " loaded with" << (ft_use_kerning?"":"out") << " kerning"); }
    else PTRACE(3,"FreeType\tCould not load truetype font: " << OpenMCU::vmcfg.fontfile);
  }
  if(ft_error) return; // Stop using freetype on fail
  unsigned int x, y, w, h, ft_fontsizepix;
  if(ft_properties & FT_P_REALTIME){ w=fw; h=fh; ft_fontsizepix=printsubs_calc(fh,vmpcfg.fontsize); x=0; y=ft_fontsizepix/2; h-=y; }
  else if(vmp.height*fw>vmp.width*fh){ w=fh*vmp.width/vmp.height; h=fh; ft_fontsizepix=printsubs_calc(fh,vmpcfg.fontsize); y=ft_fontsizepix/2; x=(fw-w)/2; h-=y; }
  else if(vmp.height*fw<vmp.width*fh){ w=fw; h=fw*vmp.height/vmp.width; ft_fontsizepix=printsubs_calc(fh,vmpcfg.fontsize); y=ft_fontsizepix/2; h-=y; x=0; y+=(fh-h)/2; }
  else { w=fw; h=fh; ft_fontsizepix=printsubs_calc(fh,vmpcfg.fontsize); x=0; y=ft_fontsizepix/2; h-=y; }
  int ft_borderxl=printsubs_calc(w,vmpcfg.border_left);
  int ft_borderxr=printsubs_calc(w,vmpcfg.border_right);
  int ft_borderyt=printsubs_calc(h,vmpcfg.border_top);
  int ft_borderyb=printsubs_calc(h,vmpcfg.border_bottom);

  if((ft_error = FT_Set_Pixel_Sizes(ft_face,0,ft_fontsizepix))) return;
  if(vmp.terminalName.GetLength()==0) if(vmp.fc<FT_SKIPFRAMES+FT_ATTEMPTS) {
    vmp.terminalName=OpenMCU::Current().GetEndpoint().GetUsername(vmp.id);
    if(vmp.terminalName.GetLength()==0) return;
  }

  struct Bitmaps{ PBYTEArray *bmp; int l, t, w, h, x; }; Bitmaps *ft_bmps=NULL; PINDEX slotCounter=0;
  int pen_x=x+ft_borderxl; int pen_y=y+ft_borderyt;
  int pen_x_max=pen_x;
  unsigned int charcode, c2, ft_previous=0;
  PINDEX len=vmp.terminalName.GetLength(); for(PINDEX i=0;i<len;++i) {
    charcode=(BYTE)vmp.terminalName[i]; if(i<len-1)c2=(BYTE)vmp.terminalName[i+1]; else c2=0;
    if(vmpcfg.cut_before_bracket) if(charcode==' ') if(c2=='[' || c2=='(') break;
    if(!(charcode&128))                                               { /* 0xxxxxxx */ } // utf-8 -> unicode
    else if(((charcode&224)==192)&&(i+1<vmp.terminalName.GetLength())){ /* 110xxxxx 10xxxxxx */ charcode=((charcode&31)<<6)+(c2&63); i++; }
    else if(((charcode&240)==224)&&(i+2<vmp.terminalName.GetLength())){ /* 1110xxxx 10xxxxxx 10xxxxxx */ charcode=((charcode&15)<<12) + (((unsigned int)(c2&63))<<6) + ((BYTE)vmp.terminalName[i+2]&63); i+=2; }
    else if(((charcode&248)==240)&&(i+3<vmp.terminalName.GetLength())){ /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */ charcode=((charcode&7)<<18) + (((unsigned int)(c2&63))<<12) + (((unsigned int)((BYTE)vmp.terminalName[i+2]&63))<<6) + ((BYTE)vmp.terminalName[i+3]&63); i+=3; }
    ft_bmps=(Bitmaps*)realloc((void *)ft_bmps,(slotCounter+1)*sizeof(Bitmaps));
    FT_GlyphSlot ft_slot=ft_face->glyph;
    ft_glyph_index=FT_Get_Char_Index(ft_face,charcode);
    if(ft_use_kerning && ft_previous && ft_glyph_index){ FT_Vector delta; FT_Get_Kerning(ft_face,ft_previous,ft_glyph_index,FT_KERNING_DEFAULT,&delta); pen_x+=delta.x>>6; }
    if( (ft_error = FT_Load_Glyph(ft_face,ft_glyph_index,FT_LOAD_RENDER)) ) {
      PTRACE(1,"FreeType\tError " << ft_error << " during FT_Load_Glyph. Debug info:\n\tvmp.fc=" << vmp.fc << "\n\tcharcode=" << charcode << "\n\ti=" << i << "/" << len << "\n\ttext=" << vmp.terminalName << "\n\n\n\n\t*** Further use of FreeType will blocked! ***\n\n\n");
      for(PINDEX i=slotCounter-1;i>=0;i--) {ft_bmps[i].bmp->SetSize(0); delete ft_bmps[i].bmp; }
      free((void*)ft_bmps); return;
    }
    Bitmaps & ft_bmp = ft_bmps[slotCounter];
    if(pen_x + (ft_slot->advance.x>>6) + ft_borderxr-x >= w){ // checking horizontal frame overflow
      if(pen_x_max < pen_x)pen_x_max=pen_x; // store max. h. pos in pen_x_max
      pen_x = x+ft_borderxl; // cr
      pen_y += ft_fontsizepix; // lf
      if(pen_y + ft_fontsizepix + ft_borderyb-y >= h) break; // checking vertical frame overflow
    }
    ft_bmp.x=pen_x-x;
    ft_bmp.l=ft_slot->bitmap_left;
    ft_bmp.t=ft_slot->bitmap_top;
    ft_bmp.w=(&ft_slot->bitmap)->width;
    ft_bmp.h=(&ft_slot->bitmap)->rows;
    ft_bmp.bmp=new PBYTEArray;
    ft_bmp.bmp->SetSize(ft_bmp.w*ft_bmp.h);
    memcpy(ft_bmp.bmp->GetPointer(),(&ft_slot->bitmap)->buffer,ft_bmp.w*ft_bmp.h);
    pen_x+=ft_slot->advance.x>>6;
    ft_previous=ft_glyph_index; slotCounter++;
  }
  if(pen_x_max < pen_x)pen_x_max=pen_x;
  int ft_width=ft_borderxr+pen_x_max-x; //pen_x_max already contain ft_borderxl
  int ft_height=ft_borderyb+pen_y+ft_fontsizepix-y; //pen_y already contain ft_borderyt
  if(ft_properties & FT_P_RIGHT) vmp.label_x=fw-x-ft_width; else if(ft_properties & FT_P_H_CENTER)vmp.label_x=(fw-ft_width)>>1; else vmp.label_x=x;
  if(ft_properties & FT_P_BOTTOM) vmp.label_y=fh-y-ft_height; else if(ft_properties & FT_P_V_CENTER)vmp.label_y=(fh-ft_height)>>1; else vmp.label_y=y;
  vmp.label_w=ft_width; vmp.label_h=ft_height;

  if(ft_properties & FT_P_REALTIME){ // rendering
    PTRACE(3,"FreeType\tSetting temp. vmp buffer size to " << (ft_width*ft_height*3/2));
    if(vmp.label_buffer.GetSize()<ft_width*ft_height*3/2)vmp.label_buffer.SetSize(ft_width*ft_height*3/2);
    FillYUVFrame_YUV(vmp.label_buffer.GetPointer(),0,vmpcfg.label_bgcolor>>8,vmpcfg.label_bgcolor&0xFF,ft_width,ft_height);
//    memset(vmp.label_buffer.GetPointer(),0,ft_width*ft_height*3/2); // remove "*3/2" ???
    pen_y=ft_borderyt;
    for(PINDEX i=0;i<slotCounter;i++){
     if(i>0) if(ft_bmps[i].x < ft_bmps[i-1].x) pen_y+=ft_fontsizepix; //lf
     CopyGrayscaleIntoFrame( ft_bmps[i].bmp->GetPointer(), vmp.label_buffer.GetPointer(),
       ft_bmps[i].l+ft_bmps[i].x, pen_y+ft_fontsizepix-ft_bmps[i].t,
       ft_bmps[i].w, ft_bmps[i].h,  ft_width, ft_height );
    }
    if(ft_properties & FT_P_TRANSPARENT) {
      MixRectIntoFrameGrayscale(vmp.label_buffer,(BYTE *)buffer,vmp.label_x,vmp.label_y,vmp.label_w,vmp.label_h,fw,fh,1);
      ReplaceUV_Rect((BYTE *)buffer,fw,fh,vmpcfg.label_bgcolor>>8,vmpcfg.label_bgcolor&0xFF,0,vmp.label_y,fw,vmp.label_h);
    }
    if(ft_properties & FT_P_SUBTITLES) MixRectIntoFrameSubsMode(vmp.label_buffer,(BYTE *)buffer,vmp.label_x,vmp.label_y,vmp.label_w,vmp.label_h,fw,fh,0);
    if(!(ft_properties & (FT_P_SUBTITLES + FT_P_TRANSPARENT))) CopyRectIntoFrame(vmp.label_buffer,(BYTE *)buffer,vmp.label_x,vmp.label_y,vmp.label_w,vmp.label_h,fw,fh);
    vmp.label_init=FALSE;
  } else { // buffering
    PTRACE(3,"FreeType\tRendering label to vmp.label_buffer[" << (ft_width*ft_height*3/2) << "]");
    if(vmp.label_buffer.GetSize()<ft_width*ft_height*3/2)vmp.label_buffer.SetSize(ft_width*ft_height*3/2);
      FillYUVFrame_YUV(vmp.label_buffer.GetPointer(),0,vmpcfg.label_bgcolor>>8,vmpcfg.label_bgcolor&0xFF,ft_width,ft_height);
      pen_y=ft_borderyt;
    for(PINDEX i=0;i<slotCounter;i++){
     if(i>0) if(ft_bmps[i].x < ft_bmps[i-1].x) pen_y+=ft_fontsizepix; //lf
     CopyGrayscaleIntoFrame( ft_bmps[i].bmp->GetPointer(), vmp.label_buffer.GetPointer(),
       ft_bmps[i].l+ft_bmps[i].x, pen_y+ft_fontsizepix-ft_bmps[i].t,
       ft_bmps[i].w, ft_bmps[i].h,  ft_width, ft_height );
    }
    vmp.label_buffer_fw=fw;
    vmp.label_buffer_fh=fh;
    vmp.label_init=TRUE;
  }
  for(PINDEX i=slotCounter-1;i>=0;i--) {ft_bmps[i].bmp->SetSize(0); delete ft_bmps[i].bmp; }
  free((void*)ft_bmps);
}
#endif

void MCUVideoMixer::ConvertRGBToYUV(BYTE R, BYTE G, BYTE B, BYTE & Y, BYTE & U, BYTE & V)
{
  Y = (BYTE)PMIN(ABS(R *  2104 + G *  4130 + B *  802 + 4096 +  131072) / 8192, 235);
  U = (BYTE)PMIN(ABS(R * -1214 + G * -2384 + B * 3598 + 4096 + 1048576) / 8192, 240);
  V = (BYTE)PMIN(ABS(R *  3598 + G * -3013 + B * -585 + 4096 + 1048576) / 8192, 240);
}

void MCUVideoMixer::FillYUVFrame(void * buffer, BYTE R, BYTE G, BYTE B, int w, int h)
{
  BYTE Y, U, V;
  ConvertRGBToYUV(R, G, B, Y, U, V);

  const int ysize = w*h;
  const int usize = (w>>1)*(h>>1);
  const int vsize = usize;

  memset((BYTE *)buffer + 0,             Y, ysize);
  memset((BYTE *)buffer + ysize,         U, usize);
  memset((BYTE *)buffer + ysize + usize, V, vsize);
}

void MCUVideoMixer::FillYUVFrame_YUV(void * buffer, BYTE Y, BYTE U, BYTE V, int w, int h)
{
  const int ysize = w*h;
  const int usize = (w/2)*(h/2);
  const int vsize = usize;

  memset((BYTE *)buffer + 0,             Y, ysize);
  memset((BYTE *)buffer + ysize,         U, usize);
  memset((BYTE *)buffer + ysize + usize, V, vsize);
}

void MCUVideoMixer::FillCIFYUVFrame(void * buffer, BYTE R, BYTE G, BYTE B)
{
  FillYUVFrame(buffer, R, G, B, CIF_WIDTH, CIF_HEIGHT);
}

void MCUVideoMixer::FillCIF4YUVFrame(void * buffer, BYTE R, BYTE G, BYTE B)
{
  FillYUVFrame(buffer, R, G, B, CIF4_WIDTH, CIF4_HEIGHT);
}

void MCUVideoMixer::FillCIF16YUVFrame(void * buffer, BYTE R, BYTE G, BYTE B)
{
  FillYUVFrame(buffer, R, G, B, CIF16_WIDTH, CIF16_HEIGHT);
}

void MCUVideoMixer::FillQCIFYUVFrame(void * buffer, BYTE R, BYTE G, BYTE B)
{
  FillYUVFrame(buffer, R, G, B, QCIF_WIDTH, QCIF_HEIGHT);
}

void MCUVideoMixer::FillCIFYUVRect(void * frame, BYTE R, BYTE G, BYTE B, int xPos, int yPos, int rectWidth, int rectHeight)
{
  FillYUVRect(frame, CIF_WIDTH, CIF_HEIGHT, R, G, B, xPos, yPos, rectWidth, rectHeight);
}

void MCUVideoMixer::FillCIF4YUVRect(void * frame, BYTE R, BYTE G, BYTE B, int xPos, int yPos, int rectWidth, int rectHeight)
{
  FillYUVRect(frame, CIF4_WIDTH, CIF4_HEIGHT, R, G, B, xPos, yPos, rectWidth, rectHeight);
}

void MCUVideoMixer::FillCIF16YUVRect(void * frame, BYTE R, BYTE G, BYTE B, int xPos, int yPos, int rectWidth, int rectHeight)
{
  FillYUVRect(frame, CIF16_WIDTH, CIF16_HEIGHT, R, G, B, xPos, yPos, rectWidth, rectHeight);
}

void MCUVideoMixer::FillYUVRect(void * frame, int frameWidth, int frameHeight, BYTE R, BYTE G, BYTE B, int xPos, int yPos, int rectWidth, int rectHeight)
{
  //This routine fills a region of the video image with data. It is used as the central
  //point because one only has to add other image formats here.

  int offset       = ( yPos * frameWidth ) + xPos;
  int colourOffset = ( (yPos * frameWidth) >> 2) + (xPos >> 1);

  BYTE Y, U, V;
  ConvertRGBToYUV(R, G, B, Y, U, V);

  BYTE * Yptr = (BYTE*)frame + offset;
  BYTE * UPtr = (BYTE*)frame + (frameWidth * frameHeight) + colourOffset;
  BYTE * VPtr = (BYTE*)frame + (frameWidth * frameHeight) + (frameWidth * frameHeight/4)  + colourOffset;

  int rr ;
  int halfRectWidth = rectWidth >> 1;
  int halfWidth     = frameWidth >> 1;
  
  for (rr = 0; rr < rectHeight;rr+=2) {
    memset(Yptr, Y, rectWidth);
    Yptr += frameWidth;
    memset(Yptr, Y, rectWidth);
    Yptr += frameWidth;

    memset(UPtr, U, halfRectWidth);
    memset(VPtr, V, halfRectWidth);

    UPtr += halfWidth;
    VPtr += halfWidth;
  }
}

void MCUVideoMixer::ReplaceUV_Rect(void * frame, int frameWidth, int frameHeight, BYTE U, BYTE V, int xPos, int yPos, int rectWidth, int rectHeight)
{
  unsigned int cw=frameWidth>>1;
  unsigned int ch=frameHeight>>1;
  unsigned int rcw=rectWidth>>1;
  unsigned int rch=rectHeight>>1;
  unsigned int offsetUV=(yPos>>1)*cw+(xPos>>1);
  unsigned int offsetU=frameWidth*frameHeight+offsetUV;
  unsigned int offsetV=cw*ch+offsetU;
  BYTE * UPtr = (BYTE*)frame + offsetU;
  BYTE * VPtr = (BYTE*)frame + offsetV;
  for (unsigned int rr=0;rr<rch;rr++) {
    memset(UPtr, U, rcw);
    memset(VPtr, V, rcw);
    UPtr += cw;
    VPtr += cw;
  }
}

void MCUVideoMixer::CopyRectIntoQCIF(const void * _src, void * _dst, int xpos, int ypos, int width, int height)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst + (ypos * QCIF_WIDTH) + xpos;

  BYTE * dstEnd = dst + QCIF_SIZE;
  int y;

  // copy Y
  for (y = 0; y < height; ++y) {
    PAssert(dst + width < dstEnd, "Y write overflow");
    memcpy(dst, src, width);
    src += width;
    dst += QCIF_WIDTH;
  }

  // copy U
  dst = (BYTE *)_dst + (QCIF_WIDTH * QCIF_HEIGHT) + (ypos * CIF_WIDTH/4) + xpos / 2;
  for (y = 0; y < height/2; ++y) {
    PAssert(dst + width/2 <= dstEnd, "U write overflow");
    memcpy(dst, src, width/2);
    src += width/2;
    dst += QCIF_WIDTH/2;
  }

  // copy V
  dst = (BYTE *)_dst + (QCIF_WIDTH * QCIF_HEIGHT) + (QCIF_WIDTH * QCIF_HEIGHT) / 4 + (ypos * QCIF_WIDTH/4) + xpos / 2;
  for (y = 0; y < height/2; ++y) {
    PAssert(dst + width/2 <= dstEnd, "V write overflow");
    memcpy(dst, src, width/2);
    src += width/2;
    dst += QCIF_WIDTH/2;
  }
}

void MCUVideoMixer::CopyRectIntoCIF(const void * _src, void * _dst, int xpos, int ypos, int width, int height)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst + (ypos * CIF_WIDTH) + xpos;

  BYTE * dstEnd = dst + CIF_SIZE;
  int y;

  // copy Y
  for (y = 0; y < height; ++y) {
    PAssert(dst + width < dstEnd, "Y write overflow");
    memcpy(dst, src, width);
    src += width;
    dst += CIF_WIDTH;
  }

  // copy U
  dst = (BYTE *)_dst + (CIF_WIDTH * CIF_HEIGHT) + (ypos * CIF_WIDTH/4) + xpos / 2;
  for (y = 0; y < height/2; ++y) {
    PAssert(dst + width/2 <= dstEnd, "U write overflow");
    memcpy(dst, src, width/2);
    src += width/2;
    dst += CIF_WIDTH/2;
  }

  // copy V
  dst = (BYTE *)_dst + (CIF_WIDTH * CIF_HEIGHT) + (CIF_WIDTH * CIF_HEIGHT) / 4 + (ypos * CIF_WIDTH/4) + xpos / 2;
  for (y = 0; y < height/2; ++y) {
    PAssert(dst + width/2 <= dstEnd, "V write overflow");
    memcpy(dst, src, width/2);
    src += width/2;
    dst += CIF_WIDTH/2;
  }
}

void MCUVideoMixer::CopyRectIntoCIF4(const void * _src, void * _dst, int xpos, int ypos, int width, int height)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst + (ypos * CIF4_WIDTH) + xpos;

  BYTE * dstEnd = dst + CIF4_SIZE;
  int y;

  // copy Y
  for (y = 0; y < height; ++y) {
    PAssert(dst + width < dstEnd, "Y write overflow");
    memcpy(dst, src, width);
    src += width;
    dst += CIF4_WIDTH;
  }

  // copy U
  dst = (BYTE *)_dst + (CIF4_WIDTH * CIF4_HEIGHT) + (ypos * CIF4_WIDTH/4) + xpos / 2;
  for (y = 0; y < height/2; ++y) {
    PAssert(dst + width/2 <= dstEnd, "U write overflow");
    memcpy(dst, src, width/2);
    src += width/2;
    dst += CIF4_WIDTH/2;
  }

  // copy V
  dst = (BYTE *)_dst + (CIF4_WIDTH * CIF4_HEIGHT) + (CIF4_WIDTH * CIF4_HEIGHT) / 4 + (ypos * CIF4_WIDTH/4) + xpos / 2;
  for (y = 0; y < height/2; ++y) {
    PAssert(dst + width/2 <= dstEnd, "V write overflow");
    memcpy(dst, src, width/2);
    src += width/2;
    dst += CIF4_WIDTH/2;
  }
}

void MCUVideoMixer::CopyGrayscaleIntoCIF(const void * _src, void * _dst, int xpos, int ypos, int width, int height)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst + (ypos * CIF_WIDTH) + xpos;
  int y;

  for (y=0;y<height;++y){
   memcpy(dst,src,width);
   src+=width;
   dst+=CIF_WIDTH;
  }
}

void MCUVideoMixer::CopyGrayscaleIntoCIF4(const void * _src, void * _dst, int xpos, int ypos, int width, int height)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst + (ypos * CIF4_WIDTH) + xpos;
  int y;

  for (y=0;y<height;++y){
   memcpy(dst,src,width);
   src+=width;
   dst+=CIF4_WIDTH;
  }
}

void MCUVideoMixer::CopyGrayscaleIntoCIF16(const void * _src, void * _dst, int xpos, int ypos, int width, int height)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst + (ypos * CIF16_WIDTH) + xpos;
  int y;

  for (y=0;y<height;++y){
   memcpy(dst,src,width);
   src+=width;
   dst+=CIF16_WIDTH;
  }
}

void MCUVideoMixer::CopyGrayscaleIntoFrame(const void * _src, void * _dst, int xpos, int ypos, int width, int height, int fw, int fh)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst + (ypos * fw) + xpos;
  int y;

  for (y=0;y<height;++y){
   memcpy(dst,src,width);
   src+=width;
   dst+=fw;
  }
}

void MCUVideoMixer::CopyRectIntoCIF16(const void * _src, void * _dst, int xpos, int ypos, int width, int height)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst + (ypos * CIF16_WIDTH) + xpos;

  BYTE * dstEnd = dst + CIF16_SIZE;
  int y;

  // copy Y
  for (y = 0; y < height; ++y) {
    PAssert(dst + width < dstEnd, "Y write overflow");
    memcpy(dst, src, width);
    src += width;
    dst += CIF16_WIDTH;
  }

  // copy U
  dst = (BYTE *)_dst + (CIF16_WIDTH * CIF16_HEIGHT) + (ypos * CIF16_WIDTH/4) + xpos / 2;
  for (y = 0; y < height/2; ++y) {
    PAssert(dst + width/2 <= dstEnd, "U write overflow");
    memcpy(dst, src, width/2);
    src += width/2;
    dst += CIF16_WIDTH/2;
  }

  // copy V
  dst = (BYTE *)_dst + (CIF16_WIDTH * CIF16_HEIGHT) + (CIF16_WIDTH * CIF16_HEIGHT) / 4 + (ypos * CIF16_WIDTH/4) + xpos / 2;
  for (y = 0; y < height/2; ++y) {
    PAssert(dst + width/2 <= dstEnd, "V write overflow");
    memcpy(dst, src, width/2);
    src += width/2;
    dst += CIF16_WIDTH/2;
  }
}


void MCUVideoMixer::CopyRFromRIntoR(const void *_s, void * _d, int xp, int yp, int w, int h, int rx_abs, int ry_abs, int rw, int rh, int fw, int fh, int lim_w, int lim_h)
{
 int rx=rx_abs-xp;
 int ry=ry_abs-yp;
 int w0=w/2;
 int ry0=ry/2;
 int rx0=rx/2;
 int fw0=fw/2;
 int rh0=rh/2;
 int rw0=rw/2;
 BYTE * s = (BYTE *)_s + w*ry + rx;
 BYTE * d = (BYTE *)_d + (yp+ry)*fw + xp + rx;
 BYTE * sU = (BYTE *)_s + w*h + ry0*w0 + rx0;
 BYTE * dU = (BYTE *)_d + fw*fh + (yp/2+ry0)*fw0 + xp/2 + rx0;
 BYTE * sV = sU + w0*(h/2);
 BYTE * dV = dU + fw0*(fh/2);

 if(rx+rw>lim_w)rw=lim_w-rx;
 if(rx0+rw0>lim_w/2)rw0=lim_w/2-rx0;
 if(ry+rh>lim_h)rh=lim_h-ry;
 if(ry0+rh0>lim_h/2)rh0=lim_h/2-ry0;

 if(rx&1){ dU++; sU++; dV++; sV++; }
// else if((rx+rw)&1)if(rx0+rw0<w0)rw0++;
 for(int i=ry;i<ry+rh;i++){
   memcpy(d,s,rw); s+=w; d+=fw;
   if(!(i&1)){
     memcpy(dU,sU,rw0); sU+=w0; dU+=fw0;
     memcpy(dV,sV,rw0); sV+=w0; dV+=fw0;
   }
 }

/*
 for(int i=0;i<rh;i++){ memcpy(d,s,rw); s+=w; d+=fw; }
 for(int i=0;i<rh0;i++){
  memcpy(dU,sU,rw0); sU+=w0; dU+=fw0;
  memcpy(dV,sV,rw0); sV+=w0; dV+=fw0;
 }
*/
}

void MCUVideoMixer::CopyRectIntoFrame(const void * _src, void * _dst, int xpos, int ypos, int width, int height, int fw, int fh)
{
 if(xpos+width > fw || ypos+height > fh) return;
 
 BYTE * src = (BYTE *)_src;
 BYTE * dst = (BYTE *)_dst + (ypos * fw) + xpos;

 int y;

  // copy Y
  for (y = 0; y < height; ++y) 
   { memcpy(dst, src, width); src += width; dst += fw; }

  // copy U
//  dst = (BYTE *)_dst + (fw * fh) + (ypos * fw >> 2) + (xpos >> 1);
  dst = (BYTE *)_dst + (fw * fh) + ((ypos>>1) * (fw>>1)) + (xpos >> 1);
  for (y = 0; y < height/2; ++y) 
   { memcpy(dst, src, width/2); src += width/2; dst += fw/2; }

  // copy V
//  dst = (BYTE *)_dst + (fw * fh) + (fw * fh >> 2) + (ypos * fw >> 2) + (xpos >> 1);
  dst = (BYTE *)_dst + (fw * fh) + ((fw>>1) * (fh>>1)) + ((ypos>>1) * (fw>>1)) + (xpos >> 1);
  for (y = 0; y < height/2; ++y) 
   { memcpy(dst, src, width/2); src += width/2; dst += fw/2; }
}

void MCUVideoMixer::MixRectIntoFrameGrayscale(const void * _src, void * _dst, int xpos, int ypos, int width, int height, int fw, int fh, BYTE wide)
{
// PTRACE(6,"FreeType\tMix/" << _src << "/" << _dst << ": (" << xpos << "," << ypos << "," << width << "," << height << ") [" << fw << "*" << fh << "]");
 if(xpos+width > fw || ypos+height > fh) return;
 BYTE * src = (BYTE *)_src;
 BYTE * dst = (BYTE *)_dst + (ypos * fw) + xpos*(1-wide);
 int y,x;
 for(y=0;y<height;y++)
 {
  if(wide)for(x=0;x<xpos;x++){ *dst>>=1; dst++; }
  for(x=0;x<width;x++) {
   if(*src>=*dst)*dst=*src; else
//   if(*src==0) *dst>>=1;
   *dst>>=1;
   src++; dst++;
  }
  if(wide)for(x=0;x<fw-width-xpos;x++){ *dst>>=1; dst++; }
  else dst+=(fw-width);
 }
}
/*
void MCUVideoMixer::MixRectIntoFrameSubsMode(const void * _src, void * _dst, int xpos, int ypos, int width, int height, int fw, int fh, BYTE wide)
{
 if(xpos+width > fw || ypos+height > fh) return;
 BYTE * src = (BYTE *)_src;
 BYTE * dst = (BYTE *)_dst + (ypos * fw) + xpos*(1-wide);
 int y,x;
 BYTE a,b,c,d;
 for(y=0;y<height;y++)
 {
//  if(wide)for(x=0;x<xpos;x++){ *dst>>=1; dst++; }
  for(x=0;x<width;x++) {
   if(*src>=*dst)*dst=*src; else
   if(*src==0)
   {
    if(x>0) a=*(src-1); else a=0;
    if(x<width-1) b=*(src+1); else b=0;
    if(y>0) c=*(src-width); else c=0;
    if(y<height-1) d=*(src+width); else d=0;
    a|=b|c|d;
    if(a) *dst>>=(4-(a>>6));
   }
   src++; dst++;
  }
//  if(wide)for(x=0;x<fw-width-xpos;x++){ *dst>>=1; dst++; }
//  else
  dst+=(fw-width);
 }
}
*/
#if USE_FREETYPE
void MCUVideoMixer::MixRectIntoFrameSubsMode(const void * _src, void * _dst, int xpos, int ypos, int width, int height, int fw, int fh, BYTE wide)
{
 if(xpos+width > fw || ypos+height > fh) return;
 BYTE * src = (BYTE *)_src;
 BYTE * dst = (BYTE *)_dst + (ypos * fw) + xpos*(1-wide);
 int y,x;
 unsigned v;
 for(y=0;y<height;y++)
 {
  for(x=0;x<width;x++) {
   if(*src>=*dst)*dst=*src; else
   if(*src==0)
   {
    if(x>0)
    {
      v=(*(src-1))<<2;
      if(y>0) v+=*(src-1-width)<<1;
      if(y<height-1) v+=*(src-1+width)<<1;
    }
    else v=0;
    if(x<width-1)
    {
      v+=*(src+1)<<2;
      if(y>0) v+=*(src+1-width)<<1;
      if(y<height-1) v+=*(src+1+width)<<1;
    }
    if(y>0) v+=*(src-width)<<2;
    if(y<height-1) v+=*(src+width)<<2;
    if(x>1)v+=*(src-2);
    if(x<width-2)v+=*(src+2);
    if(y>1)v+=*(src-width-width);
    if(y<height-2)v+=*(src+width+width);
    if(v>0) *dst=*dst*(255*28-v)/28/255;
   }
   src++; dst++;
  }
  dst+=(fw-width);
 }
}
#endif

void MCUVideoMixer::CopyRectIntoRect(const void * _src, void * _dst, int xpos, int ypos, int width, int height, int fw, int fh)
{
 if(xpos+width > fw || ypos+height > fh) return;
 
 BYTE * src = (BYTE *)_src + (ypos * fw) + xpos;
 BYTE * dst = (BYTE *)_dst + (ypos * fw) + xpos;

 int y;

  // copy Y
  for (y = 0; y < height; ++y) 
   { memcpy(dst, src, width); src += fw; dst += fw; }

  // copy U
  src = (BYTE *)_src + (fw * fh) + (ypos * fw >> 2) + (xpos >> 1);
  dst = (BYTE *)_dst + (fw * fh) + (ypos * fw >> 2) + (xpos >> 1);
  for (y = 0; y < height/2; ++y) 
   { memcpy(dst, src, width/2); src += fw/2; dst += fw/2; }

  // copy V
  src = (BYTE *)_src + (fw * fh) + (fw * fh >> 2) + (ypos * fw >> 2) + (xpos >> 1);
  dst = (BYTE *)_dst + (fw * fh) + (fw * fh >> 2) + (ypos * fw >> 2) + (xpos >> 1);
  for (y = 0; y < height/2; ++y) 
   { memcpy(dst, src, width/2); src += fw/2; dst += fw/2; }
}

void MCUVideoMixer::CopyRectFromFrame(const void * _src, void * _dst, int xpos, int ypos, int width, int height, int fw, int fh)
{
 if(xpos+width > fw || ypos+height > fh) return;
 
 BYTE * dst = (BYTE *)_dst;
 BYTE * src = (BYTE *)_src + (ypos * fw) + xpos;

 int y;

  // copy Y
  for (y = 0; y < height; ++y) 
   { memcpy(dst, src, width); dst += width; src += fw; }

  // copy U
//  src = (BYTE *)_src + (fw * fh) + (ypos * fw >> 2) + (xpos >> 1);
  src = (BYTE *)_src + (fw * fh) + ((ypos>>1) * (fw >> 1)) + (xpos >> 1);
  for (y = 0; y < height/2; ++y) 
   { memcpy(dst, src, width/2); dst += width/2; src += fw/2; }

  // copy V
//  src = (BYTE *)_src + (fw * fh) + (fw * fh >> 2) + (ypos * fw >> 2) + (xpos >> 1);
  src = (BYTE *)_src + (fw * fh) + ((fw>>1) * (fh>>1)) + ((ypos>>1) * (fw>>1)) + (xpos >> 1);
  for (y = 0; y < height/2; ++y) 
   { memcpy(dst, src, width/2); dst += width/2; src += fw/2; }
}

void MCUVideoMixer::ResizeYUV420P(const void * _src, void * _dst, unsigned int sw, unsigned int sh, unsigned int dw, unsigned int dh)
{
  if(sw==dw && sh==dh) // same size
    memcpy(_dst,_src,dw*dh*3/2);
#if USE_LIBYUV
  else libyuv::I420Scale(
    /* src_y */     (const uint8*)_src,                         /* src_stride_y */ sw,
    /* src_u */     (const uint8*)((long)_src+sw*sh),           /* src_stride_u */ (int)(sw >> 1),
    /* src_v */     (const uint8*)((long)_src+sw*sh*5/4),       /* src_stride_v */ (int)(sw >> 1),
    /* src_width */ (int)sw,                                    /* src_height */   (int)sh,
    /* dst_y */     (uint8*)_dst,                               /* dst_stride_y */ (int)dw,
    /* dst_u */     (uint8*)((long)_dst+dw*dh),                 /* dst_stride_u */ (int)(dw >> 1),
    /* dst_v */     (uint8*)((long)_dst+dw*dh+(dw>>1)*(dh>>1)), /* dst_stride_v */ (int)(dw >> 1),
    /* dst_width */ (int)dw,                                    /* dst_height */   (int)dh,
    /* filtering */ OpenMCU::Current().GetScaleFilter()
  );
#else
  else if(sw==CIF16_WIDTH && sh==CIF16_HEIGHT && dw==TCIF_WIDTH    && dh==TCIF_HEIGHT)   // CIF16 -> TCIF
    ConvertCIF16ToTCIF(_src,_dst);
  else if(sw==CIF16_WIDTH && sh==CIF16_HEIGHT && dw==Q3CIF16_WIDTH && dh==Q3CIF16_HEIGHT)// CIF16 -> Q3CIF16
    ConvertCIF16ToQ3CIF16(_src,_dst);
  else if(sw==CIF16_WIDTH && sh==CIF16_HEIGHT && dw==CIF4_WIDTH    && dh==CIF4_HEIGHT)   // CIF16 -> CIF4
    ConvertCIF16ToCIF4(_src,_dst);
  else if(sw==CIF16_WIDTH && sh==CIF16_HEIGHT && dw==Q3CIF4_WIDTH  && dh==Q3CIF4_HEIGHT) // CIF16 -> Q3CIF4
    ConvertCIF16ToQ3CIF4(_src,_dst);
  else if(sw==CIF16_WIDTH && sh==CIF16_HEIGHT && dw==CIF_WIDTH     && dh==CIF_HEIGHT)    // CIF16 -> CIF
    ConvertCIF16ToCIF(_src,_dst);

  else if(sw==CIF4_WIDTH && sh==CIF4_HEIGHT && dw==CIF16_WIDTH  && dh==CIF16_HEIGHT)  // CIF4 -> CIF16
    ConvertCIF4ToCIF16(_src,_dst);
  else if(sw==CIF4_WIDTH && sh==CIF4_HEIGHT && dw==TCIF_WIDTH   && dh==TCIF_HEIGHT)   // CIF4 -> TCIF
    ConvertCIF4ToTCIF(_src,_dst);
  else if(sw==CIF4_WIDTH && sh==CIF4_HEIGHT && dw==TQCIF_WIDTH  && dh==TQCIF_HEIGHT)  // CIF4 -> TQCIF
    ConvertCIF4ToTQCIF(_src,_dst);
  else if(sw==CIF4_WIDTH && sh==CIF4_HEIGHT && dw==CIF_WIDTH    && dh==CIF_HEIGHT)    // CIF4 -> CIF
    ConvertCIF4ToCIF(_src,_dst);
  else if(sw==CIF4_WIDTH && sh==CIF4_HEIGHT && dw==Q3CIF4_WIDTH && dh==Q3CIF4_HEIGHT) // CIF4 -> Q3CIF4
    ConvertCIF4ToQ3CIF4(_src,_dst);
  else if(sw==CIF4_WIDTH && sh==CIF4_HEIGHT && dw==QCIF_WIDTH   && dh==QCIF_HEIGHT)   // CIF4 -> QCIF
    ConvertCIF4ToQCIF(_src,_dst);
  else if(sw==CIF4_WIDTH && sh==CIF4_HEIGHT && dw==Q3CIF_WIDTH  && dh==Q3CIF_HEIGHT)  // CIF4 -> CIF16
    ConvertCIF4ToQ3CIF(_src,_dst);

  else if(sw==CIF_WIDTH && sh==CIF_HEIGHT && dw==CIF4_WIDTH   && dh==CIF4_HEIGHT)   // CIF -> CIF4
    ConvertCIFToCIF4(_src,_dst);
  else if(sw==CIF_WIDTH && sh==CIF_HEIGHT && dw==TQCIF_WIDTH  && dh==TQCIF_HEIGHT)  // CIF -> TQCIF
    ConvertCIFToTQCIF(_src,_dst);
  else if(sw==CIF_WIDTH && sh==CIF_HEIGHT && dw==TQCIF_WIDTH  && dh==TQCIF_HEIGHT)  // CIF -> TSQCIF
    ConvertCIFToTSQCIF(_src,_dst);
  else if(sw==CIF_WIDTH && sh==CIF_HEIGHT && dw==Q3CIF_WIDTH  && dh==Q3CIF_HEIGHT)  // CIF -> Q3CIF
    ConvertCIFToQ3CIF(_src,_dst);
  else if(sw==CIF_WIDTH && sh==CIF_HEIGHT && dw==QCIF_WIDTH   && dh==QCIF_HEIGHT)   // CIF -> QCIF
    ConvertCIFToQCIF(_src,_dst);
  else if(sw==CIF_WIDTH && sh==CIF_HEIGHT && dw==SQ3CIF_WIDTH && dh==SQ3CIF_HEIGHT) // CIF -> SQ3CIF
    ConvertCIFToSQ3CIF(_src,_dst);
  else if(sw==CIF_WIDTH && sh==CIF_HEIGHT && dw==SQCIF_WIDTH  && dh==SQCIF_HEIGHT)  // CIF -> SQCIF
    ConvertCIFToSQCIF(_src,_dst);

  else if(sw==QCIF_WIDTH && sh==QCIF_HEIGHT && dw==CIF4_WIDTH && dh==CIF4_HEIGHT) // QCIF -> CIF4
    ConvertQCIFToCIF4(_src,_dst);
  else if(sw==QCIF_WIDTH && sh==QCIF_HEIGHT && dw==CIF_WIDTH && dh==CIF_HEIGHT)   // QCIF -> CIF
    ConvertQCIFToCIF(_src,_dst);

  else if((sw<<1)==dw && (sh<<1)==dh) // needs 2x zoom
    Convert1To2(_src, _dst, sw, sh);
  else if((dw<<1)==sw && (dh<<1)==sh) // needs 2x reduce
    Convert2To1(_src, _dst, sw, sh);

  else ConvertFRAMEToCUSTOM_FRAME(_src,_dst,sw,sh,dw,dh);
#endif
}

#if USE_LIBYUV==0
void MCUVideoMixer::ConvertCIF4ToCIF(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;

  // copy Y
  for (y = CIF_HEIGHT; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    for (x = CIF_WIDTH; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*srcRow1+*(srcRow1+1))>>2;
      dst[0] = val;
      srcRow0 += 2; srcRow1 +=2;
      dst++;
    }
    src = srcRow1;
  }

  // copy U
  for (y = CIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    for (x = CIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*srcRow1+*(srcRow1+1))>>2;
      dst[0] = val;
      srcRow0 += 2; srcRow1 +=2;
      dst++;
    }
    src = srcRow1;
  }

  // copy V
  for (y = CIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    for (x = CIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*srcRow1+*(srcRow1+1))>>2;
      dst[0] = val;
      srcRow0 += 2; srcRow1 +=2;
      dst++;
    }
    src = srcRow1;
  }
}

void MCUVideoMixer::ConvertCIF16ToCIF4(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;

  // copy Y
  for (y = CIF4_HEIGHT; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF16_WIDTH;
    for (x = CIF4_WIDTH; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*srcRow1+*(srcRow1+1))>>2;
      dst[0] = val;
      srcRow0 += 2; srcRow1 +=2;
      dst++;
    }
    src = srcRow1;
  }

  // copy U
  for (y = CIF4_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    for (x = CIF4_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*srcRow1+*(srcRow1+1))>>2;
      dst[0] = val;
      srcRow0 += 2; srcRow1 +=2;
      dst++;
    }
    src = srcRow1;
  }

  // copy V
  for (y = CIF4_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    for (x = CIF4_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*srcRow1+*(srcRow1+1))>>2;
      dst[0] = val;
      srcRow0 += 2; srcRow1 +=2;
      dst++;
    }
    src = srcRow1;
  }
}

void MCUVideoMixer::ConvertCIFToQCIF(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;

  // copy Y
  for (y = QCIF_HEIGHT; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    for (x = QCIF_WIDTH; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*srcRow1+*(srcRow1+1))>>2;
      dst[0] = val;
      srcRow0 += 2; srcRow1 +=2;
      dst++;
    }
    src = srcRow1;
  }

  // copy U
  for (y = QCIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + QCIF_WIDTH;
    for (x = QCIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*srcRow1+*(srcRow1+1))>>2;
      dst[0] = val;
      srcRow0 += 2; srcRow1 +=2;
      dst++;
    }
    src = srcRow1;
  }

  // copy V
  for (y = QCIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + QCIF_WIDTH;
    for (x = QCIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*srcRow1+*(srcRow1+1))>>2;
      dst[0] = val;
      srcRow0 += 2; srcRow1 +=2;
      dst++;
    }
    src = srcRow1;
  }
}

void MCUVideoMixer::ConvertCIFToQ3CIF(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;
  unsigned char * srcRow2;

  // copy Y
  for (y = Q3CIF_HEIGHT; y > 1; y-=2) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    srcRow2 = src + CIF_WIDTH*2;
    for (x = Q3CIF_WIDTH; x > 1; x-=2) {
      val = (*srcRow0*4+*(srcRow0+1)*2+*srcRow1*2+*(srcRow1+1))*0.111111111;
      dst[0] = val;
      val = (*(srcRow0+1)*2+*(srcRow0+2)*4+*(srcRow1+1)+*(srcRow1+2)*2)*0.111111111;
      dst[1] = val;
      val = (*srcRow1*2+*(srcRow1+1)+*srcRow2*4+*(srcRow2+1)*2)*0.111111111;
      dst[Q3CIF_WIDTH] = val;
      val = (*(srcRow1+1)+*(srcRow1+2)*2+*(srcRow2+1)*2+*(srcRow2+2)*4)*0.111111111;
      dst[Q3CIF_WIDTH+1] = val;
      srcRow0 +=3; srcRow1 +=3; srcRow2 +=3;
      dst+=2;
    }
//    dst[0]=*srcRow0; dst[Q3CIF_WIDTH]=*srcRow2; dst++;
    dst += Q3CIF_WIDTH;
    src += CIF_WIDTH*3;
  }
  src=(unsigned char *)_src+CIF_WIDTH*CIF_HEIGHT;

  // copy U
  for (y = Q3CIF_HEIGHT/2; y > 1; y-=2) {
    srcRow0 = src;
    srcRow1 = src + QCIF_WIDTH;
    srcRow2 = src + QCIF_WIDTH*2;
    for (x = Q3CIF_WIDTH/2; x > 1; x-=2) {
      val = (*srcRow0*4+*(srcRow0+1)*2+*srcRow1*2+*(srcRow1+1))*0.111111111;
      dst[0] = val;
      val = (*(srcRow0+1)*2+*(srcRow0+2)*4+*(srcRow1+1)+*(srcRow1+2)*2)*0.111111111;
      dst[1] = val;
      val = (*srcRow1*2+*(srcRow1+1)+*srcRow2*4+*(srcRow2+1)*2)*0.111111111;
      dst[Q3CIF_WIDTH/2] = val;
      val = (*(srcRow1+1)+*(srcRow1+2)*2+*(srcRow2+1)*2+*(srcRow2+2)*4)*0.111111111;
      dst[Q3CIF_WIDTH/2+1] = val;
      srcRow0 +=3; srcRow1 +=3; srcRow2 +=3;
      dst+=2;
    }
    if(x!=0) { dst[0]=*srcRow0; dst[Q3CIF_WIDTH/2]=*srcRow2; dst++; }
    dst += Q3CIF_WIDTH/2;
    src += QCIF_WIDTH*3;
  }
  src=(unsigned char *)_src+CIF_WIDTH*CIF_HEIGHT+QCIF_WIDTH*QCIF_HEIGHT;

  // copy V
  for (y = Q3CIF_HEIGHT/2; y > 1; y-=2) {
    srcRow0 = src;
    srcRow1 = src + QCIF_WIDTH;
    srcRow2 = src + QCIF_WIDTH*2;
    for (x = Q3CIF_WIDTH/2; x > 1; x-=2) {
      val = (*srcRow0*4+*(srcRow0+1)*2+*srcRow1*2+*(srcRow1+1))*0.111111111;
      dst[0] = val;
      val = (*(srcRow0+1)*2+*(srcRow0+2)*4+*(srcRow1+1)+*(srcRow1+2)*2)*0.111111111;
      dst[1] = val;
      val = (*srcRow1*2+*(srcRow1+1)+*srcRow2*4+*(srcRow2+1)*2)*0.111111111;
      dst[Q3CIF_WIDTH/2] = val;
      val = (*(srcRow1+1)+*(srcRow1+2)*2+*(srcRow2+1)*2+*(srcRow2+2)*4)*0.111111111;
      dst[Q3CIF_WIDTH/2+1] = val;
      srcRow0 +=3; srcRow1 +=3; srcRow2 +=3;
      dst+=2;
    }
    if(x!=0) { dst[0]=*srcRow0; dst[Q3CIF_WIDTH/2]=*srcRow2; dst++; }
    dst += Q3CIF_WIDTH/2;
    src += QCIF_WIDTH*3;
  }
}

void MCUVideoMixer::ConvertCIF4ToQ3CIF4(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;
  unsigned char * srcRow2;

  // copy Y
  for (y = Q3CIF4_HEIGHT; y > 1; y-=2) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    srcRow2 = src + CIF4_WIDTH*2;
    for (x = Q3CIF4_WIDTH; x > 1; x-=2) {
      val = (*srcRow0*4+*(srcRow0+1)*2+*srcRow1*2+*(srcRow1+1))*0.111111111;
      dst[0] = val;
      val = (*(srcRow0+1)*2+*(srcRow0+2)*4+*(srcRow1+1)+*(srcRow1+2)*2)*0.111111111;
      dst[1] = val;
      val = (*srcRow1*2+*(srcRow1+1)+*srcRow2*4+*(srcRow2+1)*2)*0.111111111;
      dst[Q3CIF4_WIDTH] = val;
      val = (*(srcRow1+1)+*(srcRow1+2)*2+*(srcRow2+1)*2+*(srcRow2+2)*4)*0.111111111;
      dst[Q3CIF4_WIDTH+1] = val;
      srcRow0 +=3; srcRow1 +=3; srcRow2 +=3;
      dst+=2;
    }
//    dst[0]=*srcRow0; dst[Q3CIF_WIDTH]=*srcRow2; dst++;
    dst += Q3CIF4_WIDTH;
    src += CIF4_WIDTH*3;
  }
  src=(unsigned char *)_src+CIF4_WIDTH*CIF4_HEIGHT;

  // copy U
  for (y = Q3CIF4_HEIGHT/2; y > 1; y-=2) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    srcRow2 = src + CIF_WIDTH*2;
    for (x = Q3CIF4_WIDTH/2; x > 1; x-=2) {
      val = (*srcRow0*4+*(srcRow0+1)*2+*srcRow1*2+*(srcRow1+1))*0.111111111;
      dst[0] = val;
      val = (*(srcRow0+1)*2+*(srcRow0+2)*4+*(srcRow1+1)+*(srcRow1+2)*2)*0.111111111;
      dst[1] = val;
      val = (*srcRow1*2+*(srcRow1+1)+*srcRow2*4+*(srcRow2+1)*2)*0.111111111;
      dst[Q3CIF4_WIDTH/2] = val;
      val = (*(srcRow1+1)+*(srcRow1+2)*2+*(srcRow2+1)*2+*(srcRow2+2)*4)*0.111111111;
      dst[Q3CIF4_WIDTH/2+1] = val;
      srcRow0 +=3; srcRow1 +=3; srcRow2 +=3;
      dst+=2;
    }
    if(x!=0) { dst[0]=*srcRow0; dst[Q3CIF4_WIDTH/2]=*srcRow2; dst++; }
    dst += Q3CIF4_WIDTH/2;
    src += CIF_WIDTH*3;
  }
  src=(unsigned char *)_src+CIF4_WIDTH*CIF4_HEIGHT+CIF_WIDTH*CIF_HEIGHT;

  // copy V
  for (y = Q3CIF4_HEIGHT/2; y > 1; y-=2) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    srcRow2 = src + CIF_WIDTH*2;
    for (x = Q3CIF4_WIDTH/2; x > 1; x-=2) {
      val = (*srcRow0*4+*(srcRow0+1)*2+*srcRow1*2+*(srcRow1+1))*0.111111111;
      dst[0] = val;
      val = (*(srcRow0+1)*2+*(srcRow0+2)*4+*(srcRow1+1)+*(srcRow1+2)*2)*0.111111111;
      dst[1] = val;
      val = (*srcRow1*2+*(srcRow1+1)+*srcRow2*4+*(srcRow2+1)*2)*0.111111111;
      dst[Q3CIF4_WIDTH/2] = val;
      val = (*(srcRow1+1)+*(srcRow1+2)*2+*(srcRow2+1)*2+*(srcRow2+2)*4)*0.111111111;
      dst[Q3CIF4_WIDTH/2+1] = val;
      srcRow0 +=3; srcRow1 +=3; srcRow2 +=3;
      dst+=2;
    }
    if(x!=0) { dst[0]=*srcRow0; dst[Q3CIF4_WIDTH/2]=*srcRow2; dst++; }
    dst += Q3CIF4_WIDTH/2;
    src += CIF_WIDTH*3;
  }
}

void MCUVideoMixer::ConvertCIF16ToQ3CIF16(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;
  unsigned char * srcRow2;

  // copy Y
  for (y = Q3CIF16_HEIGHT; y > 1; y-=2) {
    srcRow0 = src;
    srcRow1 = src + CIF16_WIDTH;
    srcRow2 = src + CIF16_WIDTH*2;
    for (x = Q3CIF16_WIDTH; x > 1; x-=2) {
      val = (*srcRow0*4+*(srcRow0+1)*2+*srcRow1*2+*(srcRow1+1))*0.111111111;
      dst[0] = val;
      val = (*(srcRow0+1)*2+*(srcRow0+2)*4+*(srcRow1+1)+*(srcRow1+2)*2)*0.111111111;
      dst[1] = val;
      val = (*srcRow1*2+*(srcRow1+1)+*srcRow2*4+*(srcRow2+1)*2)*0.111111111;
      dst[Q3CIF16_WIDTH] = val;
      val = (*(srcRow1+1)+*(srcRow1+2)*2+*(srcRow2+1)*2+*(srcRow2+2)*4)*0.111111111;
      dst[Q3CIF16_WIDTH+1] = val;
      srcRow0 +=3; srcRow1 +=3; srcRow2 +=3;
      dst+=2;
    }
//    dst[0]=*srcRow0; dst[Q3CIF_WIDTH]=*srcRow2; dst++;
    dst += Q3CIF16_WIDTH;
    src += CIF16_WIDTH*3;
  }
  src=(unsigned char *)_src+CIF16_WIDTH*CIF16_HEIGHT;

  // copy U
  for (y = Q3CIF16_HEIGHT/2; y > 1; y-=2) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    srcRow2 = src + CIF4_WIDTH*2;
    for (x = Q3CIF16_WIDTH/2; x > 1; x-=2) {
      val = (*srcRow0*4+*(srcRow0+1)*2+*srcRow1*2+*(srcRow1+1))*0.111111111;
      dst[0] = val;
      val = (*(srcRow0+1)*2+*(srcRow0+2)*4+*(srcRow1+1)+*(srcRow1+2)*2)*0.111111111;
      dst[1] = val;
      val = (*srcRow1*2+*(srcRow1+1)+*srcRow2*4+*(srcRow2+1)*2)*0.111111111;
      dst[Q3CIF16_WIDTH/2] = val;
      val = (*(srcRow1+1)+*(srcRow1+2)*2+*(srcRow2+1)*2+*(srcRow2+2)*4)*0.111111111;
      dst[Q3CIF16_WIDTH/2+1] = val;
      srcRow0 +=3; srcRow1 +=3; srcRow2 +=3;
      dst+=2;
    }
    if(x!=0) { dst[0]=*srcRow0; dst[Q3CIF16_WIDTH/2]=*srcRow2; dst++; }
    dst += Q3CIF16_WIDTH/2;
    src += CIF4_WIDTH*3;
  }
  src=(unsigned char *)_src+CIF16_WIDTH*CIF16_HEIGHT+CIF4_WIDTH*CIF4_HEIGHT;

  // copy V
  for (y = Q3CIF16_HEIGHT/2; y > 1; y-=2) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    srcRow2 = src + CIF4_WIDTH*2;
    for (x = Q3CIF16_WIDTH/2; x > 1; x-=2) {
      val = (*srcRow0*4+*(srcRow0+1)*2+*srcRow1*2+*(srcRow1+1))*0.111111111;
      dst[0] = val;
      val = (*(srcRow0+1)*2+*(srcRow0+2)*4+*(srcRow1+1)+*(srcRow1+2)*2)*0.111111111;
      dst[1] = val;
      val = (*srcRow1*2+*(srcRow1+1)+*srcRow2*4+*(srcRow2+1)*2)*0.111111111;
      dst[Q3CIF16_WIDTH/2] = val;
      val = (*(srcRow1+1)+*(srcRow1+2)*2+*(srcRow2+1)*2+*(srcRow2+2)*4)*0.111111111;
      dst[Q3CIF16_WIDTH/2+1] = val;
      srcRow0 +=3; srcRow1 +=3; srcRow2 +=3;
      dst+=2;
    }
    if(x!=0) { dst[0]=*srcRow0; dst[Q3CIF16_WIDTH/2]=*srcRow2; dst++; }
    dst += Q3CIF16_WIDTH/2;
    src += CIF4_WIDTH*3;
  }
}

void MCUVideoMixer::ConvertCIF16ToTCIF(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;
  unsigned char * srcRow2;
  unsigned char * srcRow3;

  // copy Y
  for (y = TCIF_HEIGHT; y > 2; y-=3) {
    srcRow0 = src;
    srcRow1 = src + CIF16_WIDTH;
    srcRow2 = src + CIF16_WIDTH*2;
    srcRow3 = src + CIF16_WIDTH*3;
    for (x = TCIF_WIDTH; x > 2; x-=3) {
      val = (*srcRow0*9+*(srcRow0+1)*3+*srcRow1*3+*(srcRow1+1))>>4;
      dst[0] = val;
      val = (*(srcRow0+1)*6+*(srcRow0+2)*6+*(srcRow1+1)*2+*(srcRow1+2)*2)>>4;
      dst[1] = val;
      val = (*(srcRow0+2)*3+*(srcRow0+3)*9+*(srcRow1+2)+*(srcRow1+3)*3)>>4;
      dst[2] = val;
      val = (*srcRow1*6+*(srcRow1+1)*2+*srcRow2*6+*(srcRow2+1)*2)>>4;
      dst[TCIF_WIDTH] = val;
      val = (*(srcRow1+1)*4+*(srcRow1+2)*4+*(srcRow2+1)*4+*(srcRow2+2)*4)>>4;
      dst[TCIF_WIDTH+1] = val;
      val = (*(srcRow1+2)*2+*(srcRow1+3)*6+*(srcRow2+2)*2+*(srcRow2+3)*6)>>4;
      dst[TCIF_WIDTH+2] = val;
      val = (*srcRow2*3+*(srcRow2+1)*1+*srcRow3*9+*(srcRow3+1)*3)>>4;
      dst[TCIF_WIDTH*2] = val;
      val = (*(srcRow2+1)*2+*(srcRow2+2)*2+*(srcRow3+1)*6+*(srcRow3+2)*6)>>4;
      dst[TCIF_WIDTH*2+1] = val;
      val = (*(srcRow2+2)*1+*(srcRow2+3)*3+*(srcRow3+2)*3+*(srcRow3+3)*9)>>4;
      dst[TCIF_WIDTH*2+2] = val;
      srcRow0 +=4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst+=3;
    }
//    dst[0]=*srcRow0; dst[Q3CIF_WIDTH]=*srcRow2; dst++;
    dst += TCIF_WIDTH*2;
    src += CIF16_WIDTH*4;
  }
  src=(unsigned char *)_src+CIF16_WIDTH*CIF16_HEIGHT;

  // copy U
  for (y = TCIF_HEIGHT/2; y > 2; y-=3) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    srcRow2 = src + CIF4_WIDTH*2;
    srcRow3 = src + CIF4_WIDTH*3;
    for (x = TCIF_WIDTH/2; x > 2; x-=3) {
      val = (*srcRow0*9+*(srcRow0+1)*3+*srcRow1*3+*(srcRow1+1))>>4;
      dst[0] = val;
      val = (*(srcRow0+1)*6+*(srcRow0+2)*6+*(srcRow1+1)*2+*(srcRow1+2)*2)>>4;
      dst[1] = val;
      val = (*(srcRow0+2)*3+*(srcRow0+3)*9+*(srcRow1+2)+*(srcRow1+3)*3)>>4;
      dst[2] = val;
      val = (*srcRow1*6+*(srcRow1+1)*2+*srcRow2*6+*(srcRow2+1)*2)>>4;
      dst[TCIF_WIDTH/2] = val;
      val = (*(srcRow1+1)*4+*(srcRow1+2)*4+*(srcRow2+1)*4+*(srcRow2+2)*4)>>4;
      dst[TCIF_WIDTH/2+1] = val;
      val = (*(srcRow1+2)*2+*(srcRow1+3)*6+*(srcRow2+2)*2+*(srcRow2+3)*6)>>4;
      dst[TCIF_WIDTH/2+2] = val;
      val = (*srcRow2*3+*(srcRow2+1)*1+*srcRow3*9+*(srcRow3+1)*3)>>4;
      dst[TCIF_WIDTH] = val;
      val = (*(srcRow2+1)*2+*(srcRow2+2)*2+*(srcRow3+1)*6+*(srcRow3+2)*6)>>4;
      dst[TCIF_WIDTH+1] = val;
      val = (*(srcRow2+2)*1+*(srcRow2+3)*3+*(srcRow3+2)*3+*(srcRow3+3)*9)>>4;
      dst[TCIF_WIDTH+2] = val;
      srcRow0 +=4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst+=3;
    }
//    dst += x;
    dst += TCIF_WIDTH;
    src += CIF4_WIDTH*4;
  }
  src=(unsigned char *)_src+CIF16_WIDTH*CIF16_HEIGHT+CIF4_WIDTH*CIF4_HEIGHT;

  // copy V
  for (y = TCIF_HEIGHT/2; y > 2; y-=3) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    srcRow2 = src + CIF4_WIDTH*2;
    srcRow3 = src + CIF4_WIDTH*3;
    for (x = TCIF_WIDTH/2; x > 2; x-=3) {
      val = (*srcRow0*9+*(srcRow0+1)*3+*srcRow1*3+*(srcRow1+1))>>4;
      dst[0] = val;
      val = (*(srcRow0+1)*6+*(srcRow0+2)*6+*(srcRow1+1)*2+*(srcRow1+2)*2)>>4;
      dst[1] = val;
      val = (*(srcRow0+2)*3+*(srcRow0+3)*9+*(srcRow1+2)+*(srcRow1+3)*3)>>4;
      dst[2] = val;
      val = (*srcRow1*6+*(srcRow1+1)*2+*srcRow2*6+*(srcRow2+1)*2)>>4;
      dst[TCIF_WIDTH/2] = val;
      val = (*(srcRow1+1)*4+*(srcRow1+2)*4+*(srcRow2+1)*4+*(srcRow2+2)*4)>>4;
      dst[TCIF_WIDTH/2+1] = val;
      val = (*(srcRow1+2)*2+*(srcRow1+3)*6+*(srcRow2+2)*2+*(srcRow2+3)*6)>>4;
      dst[TCIF_WIDTH/2+2] = val;
      val = (*srcRow2*3+*(srcRow2+1)*1+*srcRow3*9+*(srcRow3+1)*3)>>4;
      dst[TCIF_WIDTH] = val;
      val = (*(srcRow2+1)*2+*(srcRow2+2)*2+*(srcRow3+1)*6+*(srcRow3+2)*6)>>4;
      dst[TCIF_WIDTH+1] = val;
      val = (*(srcRow2+2)*1+*(srcRow2+3)*3+*(srcRow3+2)*3+*(srcRow3+3)*9)>>4;
      dst[TCIF_WIDTH+2] = val;
      srcRow0 +=4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst+=3;
    }
//    if(x!=0) { dst[0]=*srcRow0; dst[Q3CIF16_WIDTH/2]=*srcRow2; dst++; }
    dst += TCIF_WIDTH;
    src += CIF4_WIDTH*4;
  }
}

void MCUVideoMixer::ConvertCIF4ToTQCIF(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;
  unsigned char * srcRow2;
  unsigned char * srcRow3;

  // copy Y
  for (y = TQCIF_HEIGHT; y > 2; y-=3) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    srcRow2 = src + CIF4_WIDTH*2;
    srcRow3 = src + CIF4_WIDTH*3;
    for (x = TQCIF_WIDTH; x > 2; x-=3) {
      val = (*srcRow0*9+*(srcRow0+1)*3+*srcRow1*3+*(srcRow1+1))>>4;
      dst[0] = val;
      val = (*(srcRow0+1)*6+*(srcRow0+2)*6+*(srcRow1+1)*2+*(srcRow1+2)*2)>>4;
      dst[1] = val;
      val = (*(srcRow0+2)*3+*(srcRow0+3)*9+*(srcRow1+2)+*(srcRow1+3)*3)>>4;
      dst[2] = val;
      val = (*srcRow1*6+*(srcRow1+1)*2+*srcRow2*6+*(srcRow2+1)*2)>>4;
      dst[TQCIF_WIDTH] = val;
      val = (*(srcRow1+1)*4+*(srcRow1+2)*4+*(srcRow2+1)*4+*(srcRow2+2)*4)>>4;
      dst[TQCIF_WIDTH+1] = val;
      val = (*(srcRow1+2)*2+*(srcRow1+3)*6+*(srcRow2+2)*2+*(srcRow2+3)*6)>>4;
      dst[TQCIF_WIDTH+2] = val;
      val = (*srcRow2*3+*(srcRow2+1)*1+*srcRow3*9+*(srcRow3+1)*3)>>4;
      dst[TQCIF_WIDTH*2] = val;
      val = (*(srcRow2+1)*2+*(srcRow2+2)*2+*(srcRow3+1)*6+*(srcRow3+2)*6)>>4;
      dst[TQCIF_WIDTH*2+1] = val;
      val = (*(srcRow2+2)*1+*(srcRow2+3)*3+*(srcRow3+2)*3+*(srcRow3+3)*9)>>4;
      dst[TQCIF_WIDTH*2+2] = val;
      srcRow0 +=4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst+=3;
    }
//    dst[0]=*srcRow0; dst[Q3CIF_WIDTH]=*srcRow2; dst++;
    dst += TQCIF_WIDTH*2;
    src += CIF4_WIDTH*4;
  }
  src=(unsigned char *)_src+CIF4_WIDTH*CIF4_HEIGHT;

  // copy U
  for (y = TQCIF_HEIGHT/2; y > 2; y-=3) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    srcRow2 = src + CIF_WIDTH*2;
    srcRow3 = src + CIF_WIDTH*3;
    for (x = TQCIF_WIDTH/2; x > 2; x-=3) {
      val = (*srcRow0*9+*(srcRow0+1)*3+*srcRow1*3+*(srcRow1+1))>>4;
      dst[0] = val;
      val = (*(srcRow0+1)*6+*(srcRow0+2)*6+*(srcRow1+1)*2+*(srcRow1+2)*2)>>4;
      dst[1] = val;
      val = (*(srcRow0+2)*3+*(srcRow0+3)*9+*(srcRow1+2)+*(srcRow1+3)*3)>>4;
      dst[2] = val;
      val = (*srcRow1*6+*(srcRow1+1)*2+*srcRow2*6+*(srcRow2+1)*2)>>4;
      dst[TQCIF_WIDTH/2] = val;
      val = (*(srcRow1+1)*4+*(srcRow1+2)*4+*(srcRow2+1)*4+*(srcRow2+2)*4)>>4;
      dst[TQCIF_WIDTH/2+1] = val;
      val = (*(srcRow1+2)*2+*(srcRow1+3)*6+*(srcRow2+2)*2+*(srcRow2+3)*6)>>4;
      dst[TQCIF_WIDTH/2+2] = val;
      val = (*srcRow2*3+*(srcRow2+1)*1+*srcRow3*9+*(srcRow3+1)*3)>>4;
      dst[TQCIF_WIDTH] = val;
      val = (*(srcRow2+1)*2+*(srcRow2+2)*2+*(srcRow3+1)*6+*(srcRow3+2)*6)>>4;
      dst[TQCIF_WIDTH+1] = val;
      val = (*(srcRow2+2)*1+*(srcRow2+3)*3+*(srcRow3+2)*3+*(srcRow3+3)*9)>>4;
      dst[TQCIF_WIDTH+2] = val;
      srcRow0 +=4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst+=3;
    }
//    dst += x;
    dst += TQCIF_WIDTH;
    src += CIF_WIDTH*4;
  }
  src=(unsigned char *)_src+CIF4_WIDTH*CIF4_HEIGHT+CIF_WIDTH*CIF_HEIGHT;

  // copy V
  for (y = TQCIF_HEIGHT/2; y > 2; y-=3) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    srcRow2 = src + CIF_WIDTH*2;
    srcRow3 = src + CIF_WIDTH*3;
    for (x = TQCIF_WIDTH/2; x > 2; x-=3) {
      val = (*srcRow0*9+*(srcRow0+1)*3+*srcRow1*3+*(srcRow1+1))>>4;
      dst[0] = val;
      val = (*(srcRow0+1)*6+*(srcRow0+2)*6+*(srcRow1+1)*2+*(srcRow1+2)*2)>>4;
      dst[1] = val;
      val = (*(srcRow0+2)*3+*(srcRow0+3)*9+*(srcRow1+2)+*(srcRow1+3)*3)>>4;
      dst[2] = val;
      val = (*srcRow1*6+*(srcRow1+1)*2+*srcRow2*6+*(srcRow2+1)*2)>>4;
      dst[TQCIF_WIDTH/2] = val;
      val = (*(srcRow1+1)*4+*(srcRow1+2)*4+*(srcRow2+1)*4+*(srcRow2+2)*4)>>4;
      dst[TQCIF_WIDTH/2+1] = val;
      val = (*(srcRow1+2)*2+*(srcRow1+3)*6+*(srcRow2+2)*2+*(srcRow2+3)*6)>>4;
      dst[TQCIF_WIDTH/2+2] = val;
      val = (*srcRow2*3+*(srcRow2+1)*1+*srcRow3*9+*(srcRow3+1)*3)>>4;
      dst[TQCIF_WIDTH] = val;
      val = (*(srcRow2+1)*2+*(srcRow2+2)*2+*(srcRow3+1)*6+*(srcRow3+2)*6)>>4;
      dst[TQCIF_WIDTH+1] = val;
      val = (*(srcRow2+2)*1+*(srcRow2+3)*3+*(srcRow3+2)*3+*(srcRow3+3)*9)>>4;
      dst[TQCIF_WIDTH+2] = val;
      srcRow0 +=4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst+=3;
    }
//    if(x!=0) { dst[0]=*srcRow0; dst[Q3CIF16_WIDTH/2]=*srcRow2; dst++; }
    dst += TQCIF_WIDTH;
    src += CIF_WIDTH*4;
  }
}

void MCUVideoMixer::ConvertCIFToTSQCIF(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;
  unsigned char * srcRow2;
  unsigned char * srcRow3;

  // copy Y
  for (y = TSQCIF_HEIGHT; y > 2; y-=3) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    srcRow2 = src + CIF_WIDTH*2;
    srcRow3 = src + CIF_WIDTH*3;
    for (x = TSQCIF_WIDTH; x > 2; x-=3) {
      val = (*srcRow0*9+*(srcRow0+1)*3+*srcRow1*3+*(srcRow1+1))>>4;
      dst[0] = val;
      val = (*(srcRow0+1)*6+*(srcRow0+2)*6+*(srcRow1+1)*2+*(srcRow1+2)*2)>>4;
      dst[1] = val;
      val = (*(srcRow0+2)*3+*(srcRow0+3)*9+*(srcRow1+2)+*(srcRow1+3)*3)>>4;
      dst[2] = val;
      val = (*srcRow1*6+*(srcRow1+1)*2+*srcRow2*6+*(srcRow2+1)*2)>>4;
      dst[TSQCIF_WIDTH] = val;
      val = (*(srcRow1+1)*4+*(srcRow1+2)*4+*(srcRow2+1)*4+*(srcRow2+2)*4)>>4;
      dst[TSQCIF_WIDTH+1] = val;
      val = (*(srcRow1+2)*2+*(srcRow1+3)*6+*(srcRow2+2)*2+*(srcRow2+3)*6)>>4;
      dst[TSQCIF_WIDTH+2] = val;
      val = (*srcRow2*3+*(srcRow2+1)*1+*srcRow3*9+*(srcRow3+1)*3)>>4;
      dst[TSQCIF_WIDTH*2] = val;
      val = (*(srcRow2+1)*2+*(srcRow2+2)*2+*(srcRow3+1)*6+*(srcRow3+2)*6)>>4;
      dst[TSQCIF_WIDTH*2+1] = val;
      val = (*(srcRow2+2)*1+*(srcRow2+3)*3+*(srcRow3+2)*3+*(srcRow3+3)*9)>>4;
      dst[TSQCIF_WIDTH*2+2] = val;
      srcRow0 +=4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst+=3;
    }
//    dst[0]=*srcRow0; dst[Q3CIF_WIDTH]=*srcRow2; dst++;
    dst += TSQCIF_WIDTH*2;
    src += CIF_WIDTH*4;
  }
  src=(unsigned char *)_src+CIF_WIDTH*CIF_HEIGHT;

  // copy U
  for (y = TSQCIF_HEIGHT/2; y > 2; y-=3) {
    srcRow0 = src;
    srcRow1 = src + QCIF_WIDTH;
    srcRow2 = src + QCIF_WIDTH*2;
    srcRow3 = src + QCIF_WIDTH*3;
    for (x = TSQCIF_WIDTH/2; x > 2; x-=3) {
      val = (*srcRow0*9+*(srcRow0+1)*3+*srcRow1*3+*(srcRow1+1))>>4;
      dst[0] = val;
      val = (*(srcRow0+1)*6+*(srcRow0+2)*6+*(srcRow1+1)*2+*(srcRow1+2)*2)>>4;
      dst[1] = val;
      val = (*(srcRow0+2)*3+*(srcRow0+3)*9+*(srcRow1+2)+*(srcRow1+3)*3)>>4;
      dst[2] = val;
      val = (*srcRow1*6+*(srcRow1+1)*2+*srcRow2*6+*(srcRow2+1)*2)>>4;
      dst[TSQCIF_WIDTH/2] = val;
      val = (*(srcRow1+1)*4+*(srcRow1+2)*4+*(srcRow2+1)*4+*(srcRow2+2)*4)>>4;
      dst[TSQCIF_WIDTH/2+1] = val;
      val = (*(srcRow1+2)*2+*(srcRow1+3)*6+*(srcRow2+2)*2+*(srcRow2+3)*6)>>4;
      dst[TSQCIF_WIDTH/2+2] = val;
      val = (*srcRow2*3+*(srcRow2+1)*1+*srcRow3*9+*(srcRow3+1)*3)>>4;
      dst[TSQCIF_WIDTH] = val;
      val = (*(srcRow2+1)*2+*(srcRow2+2)*2+*(srcRow3+1)*6+*(srcRow3+2)*6)>>4;
      dst[TSQCIF_WIDTH+1] = val;
      val = (*(srcRow2+2)*1+*(srcRow2+3)*3+*(srcRow3+2)*3+*(srcRow3+3)*9)>>4;
      dst[TSQCIF_WIDTH+2] = val;
      srcRow0 +=4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst+=3;
    }
//    dst += x;
    dst += TSQCIF_WIDTH;
    src += QCIF_WIDTH*4;
  }
  src=(unsigned char *)_src+CIF_WIDTH*CIF_HEIGHT+QCIF_WIDTH*QCIF_HEIGHT;

  // copy V
  for (y = TSQCIF_HEIGHT/2; y > 2; y-=3) {
    srcRow0 = src;
    srcRow1 = src + QCIF_WIDTH;
    srcRow2 = src + QCIF_WIDTH*2;
    srcRow3 = src + QCIF_WIDTH*3;
    for (x = TSQCIF_WIDTH/2; x > 2; x-=3) {
      val = (*srcRow0*9+*(srcRow0+1)*3+*srcRow1*3+*(srcRow1+1))>>4;
      dst[0] = val;
      val = (*(srcRow0+1)*6+*(srcRow0+2)*6+*(srcRow1+1)*2+*(srcRow1+2)*2)>>4;
      dst[1] = val;
      val = (*(srcRow0+2)*3+*(srcRow0+3)*9+*(srcRow1+2)+*(srcRow1+3)*3)>>4;
      dst[2] = val;
      val = (*srcRow1*6+*(srcRow1+1)*2+*srcRow2*6+*(srcRow2+1)*2)>>4;
      dst[TSQCIF_WIDTH/2] = val;
      val = (*(srcRow1+1)*4+*(srcRow1+2)*4+*(srcRow2+1)*4+*(srcRow2+2)*4)>>4;
      dst[TSQCIF_WIDTH/2+1] = val;
      val = (*(srcRow1+2)*2+*(srcRow1+3)*6+*(srcRow2+2)*2+*(srcRow2+3)*6)>>4;
      dst[TSQCIF_WIDTH/2+2] = val;
      val = (*srcRow2*3+*(srcRow2+1)*1+*srcRow3*9+*(srcRow3+1)*3)>>4;
      dst[TSQCIF_WIDTH] = val;
      val = (*(srcRow2+1)*2+*(srcRow2+2)*2+*(srcRow3+1)*6+*(srcRow3+2)*6)>>4;
      dst[TSQCIF_WIDTH+1] = val;
      val = (*(srcRow2+2)*1+*(srcRow2+3)*3+*(srcRow3+2)*3+*(srcRow3+3)*9)>>4;
      dst[TSQCIF_WIDTH+2] = val;
      srcRow0 +=4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst+=3;
    }
//    if(x!=0) { dst[0]=*srcRow0; dst[Q3CIF16_WIDTH/2]=*srcRow2; dst++; }
    dst += TSQCIF_WIDTH;
    src += QCIF_WIDTH*4;
  }
}


void MCUVideoMixer::Convert2To1(const void * _src, void * _dst, unsigned int w, unsigned int h)
{
 if(w==CIF16_WIDTH && h==CIF16_HEIGHT) { ConvertCIF16ToCIF4(_src,_dst); return; }
 if(w==CIF4_WIDTH && h==CIF4_HEIGHT) { ConvertCIF4ToCIF(_src,_dst); return; }
 if(w==CIF_WIDTH && h==CIF_HEIGHT) { ConvertCIFToQCIF(_src,_dst); return; }
// if(w==QCIF_WIDTH && h=QCIF_HEIGHT) { ConvertQCIFToSQCIF(_src,_dst); return; }

  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;

  // copy Y
  for (y = h>>1; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + w;
    for (x = w>>1; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*srcRow1+*(srcRow1+1))>>2;
      dst[0] = val;
      srcRow0 += 2; srcRow1 +=2;
      dst++;
    }
    src = srcRow1;
  }

  // copy U
  for (y = h>>2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + (w>>1);
    for (x = w>>2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*srcRow1+*(srcRow1+1))>>2;
      dst[0] = val;
      srcRow0 += 2; srcRow1 +=2;
      dst++;
    }
    src = srcRow1;
  }

  // copy V
  for (y = h>>2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + (w>>1);
    for (x = w>>2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*srcRow1+*(srcRow1+1))>>2;
      dst[0] = val;
      srcRow0 += 2; srcRow1 +=2;
      dst++;
    }
    src = srcRow1;
  }
}

void MCUVideoMixer::ConvertCIFToSQ3CIF(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;
  unsigned char * srcRow2;

  // copy Y
  for (y = SQ3CIF_HEIGHT; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    srcRow2 = src + CIF_WIDTH*2;
    for (x = SQ3CIF_WIDTH; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2))*0.11111111111;
      dst[0] = val;
      srcRow0 += 3; srcRow1 +=3; srcRow2 +=3;
      dst++;
    }
    src += CIF_WIDTH*3;
  }

  // copy U
  for (y = SQ3CIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + QCIF_WIDTH;
    srcRow2 = src + QCIF_WIDTH*2;
    for (x = SQ3CIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2))*0.11111111111;
      dst[0] = val;
      srcRow0 += 3; srcRow1 +=3; srcRow2 +=3;
      dst++;
    }
    src += QCIF_WIDTH*3;
  }

  // copy V
  for (y = SQ3CIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + QCIF_WIDTH;
    srcRow2 = src + QCIF_WIDTH*2;
    for (x = SQ3CIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2))*0.11111111111;
      dst[0] = val;
      srcRow0 += 3; srcRow1 +=3; srcRow2 +=3;
      dst++;
    }
    src += QCIF_WIDTH*3;
  }
}

void MCUVideoMixer::ConvertCIF4ToQ3CIF(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;
  unsigned char * srcRow2;

  // copy Y
  for (y = Q3CIF_HEIGHT; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    srcRow2 = src + CIF4_WIDTH*2;
    for (x = Q3CIF_WIDTH; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2))*0.11111111111;
      dst[0] = val;
      srcRow0 += 3; srcRow1 +=3; srcRow2 +=3;
      dst++;
    }
    src += CIF4_WIDTH*3;
  }

  // copy U
  for (y = Q3CIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    srcRow2 = src + CIF_WIDTH*2;
    for (x = Q3CIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2))*0.11111111111;
      dst[0] = val;
      srcRow0 += 3; srcRow1 +=3; srcRow2 +=3;
      dst++;
    }
    src += CIF_WIDTH*3;
  }

  // copy V
  for (y = Q3CIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    srcRow2 = src + CIF_WIDTH*2;
    for (x = Q3CIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2))*0.11111111111;
      dst[0] = val;
      srcRow0 += 3; srcRow1 +=3; srcRow2 +=3;
      dst++;
    }
    src += CIF_WIDTH*3;
  }
}

void MCUVideoMixer::ConvertCIF16ToQ3CIF4(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;
  unsigned char * srcRow2;

  // copy Y
  for (y = Q3CIF4_HEIGHT; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF16_WIDTH;
    srcRow2 = src + CIF16_WIDTH*2;
    for (x = Q3CIF4_WIDTH; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2))*0.11111111111;
      dst[0] = val;
      srcRow0 += 3; srcRow1 +=3; srcRow2 +=3;
      dst++;
    }
    src += CIF16_WIDTH*3;
  }

  // copy U
  for (y = Q3CIF4_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    srcRow2 = src + CIF4_WIDTH*2;
    for (x = Q3CIF4_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2))*0.11111111111;
      dst[0] = val;
      srcRow0 += 3; srcRow1 +=3; srcRow2 +=3;
      dst++;
    }
    src += CIF4_WIDTH*3;
  }

  // copy V
  for (y = Q3CIF4_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    srcRow2 = src + CIF4_WIDTH*2;
    for (x = Q3CIF4_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2))*0.11111111111;
      dst[0] = val;
      srcRow0 += 3; srcRow1 +=3; srcRow2 +=3;
      dst++;
    }
    src += CIF4_WIDTH*3;
  }
}

void MCUVideoMixer::ConvertCIFToSQCIF(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;
  unsigned char * srcRow2;
  unsigned char * srcRow3;

  // copy Y
  for (y = SQCIF_HEIGHT; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    srcRow2 = src + CIF_WIDTH*2;
    srcRow3 = src + CIF_WIDTH*3;
    for (x = SQCIF_WIDTH; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)+*(srcRow0+3)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)+*(srcRow1+3)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2)+*(srcRow2+3)
    	    +*srcRow3+*(srcRow3+1)+*(srcRow3+2)+*(srcRow3+3))>>4;
      dst[0] = val;
      srcRow0 += 4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst++;
    }
    src += CIF_WIDTH*4;
  }

  // copy U
  for (y = SQCIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + QCIF_WIDTH;
    srcRow2 = src + QCIF_WIDTH*2;
    srcRow3 = src + QCIF_WIDTH*3;
    for (x = SQCIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)+*(srcRow0+3)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)+*(srcRow1+3)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2)+*(srcRow2+3)
    	    +*srcRow3+*(srcRow3+1)+*(srcRow3+2)+*(srcRow3+3))>>4;
      dst[0] = val;
      srcRow0 += 4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst++;
    }
    src += QCIF_WIDTH*4;
  }

  // copy V
  for (y = SQCIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + QCIF_WIDTH;
    srcRow2 = src + QCIF_WIDTH*2;
    srcRow3 = src + QCIF_WIDTH*3;
    for (x = SQCIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)+*(srcRow0+3)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)+*(srcRow1+3)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2)+*(srcRow2+3)
    	    +*srcRow3+*(srcRow3+1)+*(srcRow3+2)+*(srcRow3+3))>>4;
      dst[0] = val;
      srcRow0 += 4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst++;
    }
    src += QCIF_WIDTH*4;
  }
}

void MCUVideoMixer::ConvertCIF4ToQCIF(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;
  unsigned char * srcRow2;
  unsigned char * srcRow3;

  // copy Y
  for (y = QCIF_HEIGHT; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    srcRow2 = src + CIF4_WIDTH*2;
    srcRow3 = src + CIF4_WIDTH*3;
    for (x = QCIF_WIDTH; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)+*(srcRow0+3)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)+*(srcRow1+3)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2)+*(srcRow2+3)
    	    +*srcRow3+*(srcRow3+1)+*(srcRow3+2)+*(srcRow3+3))>>4;
      dst[0] = val;
      srcRow0 += 4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst++;
    }
    src += CIF4_WIDTH*4;
  }

  // copy U
  for (y = QCIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    srcRow2 = src + CIF_WIDTH*2;
    srcRow3 = src + CIF_WIDTH*3;
    for (x = QCIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)+*(srcRow0+3)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)+*(srcRow1+3)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2)+*(srcRow2+3)
    	    +*srcRow3+*(srcRow3+1)+*(srcRow3+2)+*(srcRow3+3))>>4;
      dst[0] = val;
      srcRow0 += 4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst++;
    }
    src += CIF_WIDTH*4;
  }

  // copy V
  for (y = QCIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF_WIDTH;
    srcRow2 = src + CIF_WIDTH*2;
    srcRow3 = src + CIF_WIDTH*3;
    for (x = QCIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)+*(srcRow0+3)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)+*(srcRow1+3)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2)+*(srcRow2+3)
    	    +*srcRow3+*(srcRow3+1)+*(srcRow3+2)+*(srcRow3+3))>>4;
      dst[0] = val;
      srcRow0 += 4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst++;
    }
    src += CIF_WIDTH*4;
  }
}

void MCUVideoMixer::ConvertCIF16ToCIF(const void * _src, void * _dst)
{
  unsigned char * src = (unsigned char *)_src;
  unsigned char * dst = (unsigned char *)_dst;

  unsigned int y, x, val;
  unsigned char * srcRow0;
  unsigned char * srcRow1;
  unsigned char * srcRow2;
  unsigned char * srcRow3;

  // copy Y
  for (y = CIF_HEIGHT; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF16_WIDTH;
    srcRow2 = src + CIF16_WIDTH*2;
    srcRow3 = src + CIF16_WIDTH*3;
    for (x = CIF_WIDTH; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)+*(srcRow0+3)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)+*(srcRow1+3)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2)+*(srcRow2+3)
    	    +*srcRow3+*(srcRow3+1)+*(srcRow3+2)+*(srcRow3+3))>>4;
      dst[0] = val;
      srcRow0 += 4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst++;
    }
    src += CIF16_WIDTH*4;
  }

  // copy U
  for (y = CIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    srcRow2 = src + CIF4_WIDTH*2;
    srcRow3 = src + CIF4_WIDTH*3;
    for (x = CIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)+*(srcRow0+3)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)+*(srcRow1+3)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2)+*(srcRow2+3)
    	    +*srcRow3+*(srcRow3+1)+*(srcRow3+2)+*(srcRow3+3))>>4;
      dst[0] = val;
      srcRow0 += 4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst++;
    }
    src += CIF4_WIDTH*4;
  }

  // copy V
  for (y = CIF_HEIGHT/2; y > 0; y--) {
    srcRow0 = src;
    srcRow1 = src + CIF4_WIDTH;
    srcRow2 = src + CIF4_WIDTH*2;
    srcRow3 = src + CIF4_WIDTH*3;
    for (x = CIF_WIDTH/2; x > 0; x--) {
      val = (*srcRow0+*(srcRow0+1)+*(srcRow0+2)+*(srcRow0+3)
    	    +*srcRow1+*(srcRow1+1)+*(srcRow1+2)+*(srcRow1+3)
    	    +*srcRow2+*(srcRow2+1)+*(srcRow2+2)+*(srcRow2+3)
    	    +*srcRow3+*(srcRow3+1)+*(srcRow3+2)+*(srcRow3+3))>>4;
      dst[0] = val;
      srcRow0 += 4; srcRow1 +=4; srcRow2 +=4; srcRow3 +=4;
      dst++;
    }
    src += CIF4_WIDTH*4;
  }
}

void MCUVideoMixer::ConvertFRAMEToCUSTOM_FRAME(const void * _src, void * _dst, unsigned int sw, unsigned int sh, unsigned int dw, unsigned int dh)
{
 BYTE * src = (BYTE *)_src;
 BYTE * dst = (BYTE *)_dst;

 //BYTE * dstEnd = dst + CIF_SIZE;
 int y, x, cx, cy;
 BYTE * srcRow;

  // copy Y
  cy=-dh;
  for (y = dh; y > 0; y--) {
    srcRow = src; cx=-dw;
    for (x = dw; x > 0; x--) {
      *dst = *srcRow;
      cx+=sw; while(cx>=0) { cx-=dw; srcRow++; }
      dst++;
    }
    cy+=sh; while(cy>=0) { cy-=dh; src+=sw; }
  }
  // copy U
  src=(BYTE *)_src+(sw*sh);
  cy=-dh;
  for (y = dh/2; y > 0; y--) {
    srcRow = src; cx=-dw;
    for (x = (dw>>1); x > 0; x--) {
      *dst = *srcRow;
      cx+=sw; while(cx>=0) { cx-=dw; srcRow++; }
      dst++;
    }
    cy+=sh; while(cy>=0) { cy-=dh; src+=(sw>>1); }
  }

  // copy V
  src=(BYTE *)_src+(sw*sh)+((sw/2)*(sh/2));
  cy=-dh;
  for (y = dh/2; y > 0; y--) {
    srcRow = src; cx=-dw;
    for (x = (dw>>1); x > 0; x--) {
      *dst = *srcRow;
      cx+=sw; while(cx>=0) { cx-=dw; srcRow++; }
      dst++;
    }
    cy+=sh; while(cy>=0) { cy-=dh; src+=(sw>>1); }
  }

}

void MCUVideoMixer::ConvertQCIFToCIF(const void * _src, void * _dst)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst;

  //BYTE * dstEnd = dst + CIF_SIZE;
  int y, x;
  BYTE * srcRow;

  // copy Y
  srcRow = src;
  for (y = 1; y < QCIF_HEIGHT; y++) 
  {
    for (x = 1; x < QCIF_WIDTH; x++) 
    {
      dst[0] = srcRow[0];
      dst[1] = (srcRow[0]+srcRow[1])>>1;
      dst[QCIF_WIDTH*2] = (srcRow[0]+srcRow[QCIF_WIDTH])>>1;
      dst[QCIF_WIDTH*2+1] = (srcRow[0]+srcRow[1]+srcRow[QCIF_WIDTH]+srcRow[QCIF_WIDTH+1])>>2;
      dst+=2; srcRow++;
    }
    dst[0] = dst[1] = srcRow[0];
    dst[QCIF_WIDTH*2] = dst[QCIF_WIDTH*2+1] = (srcRow[0]+srcRow[QCIF_WIDTH])>>1;
    srcRow++; dst += 2; dst += QCIF_WIDTH*2;
  }
  for (x = 1; x < QCIF_WIDTH; x++) 
  {
   dst[0] = dst[QCIF_WIDTH*2] = srcRow[0];
   dst[1] = dst[QCIF_WIDTH*2+1] = (srcRow[0]+srcRow[1])>>1;
   dst+=2; srcRow++;
  }
  dst[0] = dst[1] = dst[QCIF_WIDTH*2] = dst[QCIF_WIDTH*2+1] = srcRow[0];
  srcRow++; dst += 2; dst += QCIF_WIDTH*2;

  for (y = 1; y < QCIF_HEIGHT/2; y++) 
  {
    for (x = 1; x < QCIF_WIDTH/2; x++) 
    {
      dst[0] = srcRow[0];
      dst[1] = (srcRow[0]+srcRow[1])>>1;
      dst[QCIF_WIDTH] = (srcRow[0]+srcRow[QCIF_WIDTH/2])>>1;
      dst[QCIF_WIDTH+1] = (srcRow[0]+srcRow[1]+srcRow[QCIF_WIDTH/2]+srcRow[QCIF_WIDTH/2+1])>>2;
      dst+=2; srcRow++;
    }
    dst[0] = dst[1] = srcRow[0];
    dst[QCIF_WIDTH] = dst[QCIF_WIDTH+1] = (srcRow[0]+srcRow[QCIF_WIDTH/2])>>1;
    srcRow++; dst += 2; dst += QCIF_WIDTH;
  }
  for (x = 1; x < QCIF_WIDTH/2; x++) 
  {
   dst[0] = dst[QCIF_WIDTH] = srcRow[0];
   dst[1] = dst[QCIF_WIDTH+1] = (srcRow[0]+srcRow[1])>>1;
   dst+=2; srcRow++;
  }
  dst[0] = dst[1] = dst[QCIF_WIDTH] = dst[QCIF_WIDTH+1] = srcRow[0];
  srcRow++; dst += 2; dst += QCIF_WIDTH;

  for (y = 1; y < QCIF_HEIGHT/2; y++) 
  {
    for (x = 1; x < QCIF_WIDTH/2; x++) 
    {
      dst[0] = srcRow[0];
      dst[1] = (srcRow[0]+srcRow[1])>>1;
      dst[QCIF_WIDTH] = (srcRow[0]+srcRow[QCIF_WIDTH/2])>>1;
      dst[QCIF_WIDTH+1] = (srcRow[0]+srcRow[1]+srcRow[QCIF_WIDTH/2]+srcRow[QCIF_WIDTH/2+1])>>2;
      dst+=2; srcRow++;
    }
    dst[0] = dst[1] = srcRow[0];
    dst[QCIF_WIDTH] = dst[QCIF_WIDTH+1] = (srcRow[0]+srcRow[QCIF_WIDTH/2])>>1;
    srcRow++; dst += 2; dst += QCIF_WIDTH;
  }
  for (x = 1; x < QCIF_WIDTH/2; x++) 
  {
   dst[0] = dst[QCIF_WIDTH] = srcRow[0];
   dst[1] = dst[QCIF_WIDTH+1] = (srcRow[0]+srcRow[1])>>1;
   dst+=2; srcRow++;
  }
  dst[0] = dst[1] = dst[QCIF_WIDTH] = dst[QCIF_WIDTH+1] = srcRow[0];
  srcRow++; dst += 2; dst += QCIF_WIDTH;
}


void MCUVideoMixer::ConvertCIFToCIF4(const void * _src, void * _dst)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst;

  //BYTE * dstEnd = dst + CIF_SIZE;
  int y, x;
  BYTE * srcRow;

  // copy Y
  srcRow = src;
  for (y = 1; y < CIF_HEIGHT; y++) 
  {
    for (x = 1; x < CIF_WIDTH; x++) 
    {
      dst[0] = srcRow[0];
      dst[1] = (srcRow[0]+srcRow[1])>>1;
      dst[CIF_WIDTH*2] = (srcRow[0]+srcRow[CIF_WIDTH])>>1;
      dst[CIF_WIDTH*2+1] = (srcRow[0]+srcRow[1]+srcRow[CIF_WIDTH]+srcRow[CIF_WIDTH+1])>>2;
      dst+=2; srcRow++;
    }
    dst[0] = dst[1] = srcRow[0];
    dst[CIF_WIDTH*2] = dst[CIF_WIDTH*2+1] = (srcRow[0]+srcRow[CIF_WIDTH])>>1;
    srcRow++; dst += 2; dst += CIF_WIDTH*2;
  }
  for (x = 1; x < CIF_WIDTH; x++) 
  {
   dst[0] = dst[CIF_WIDTH*2] = srcRow[0];
   dst[1] = dst[CIF_WIDTH*2+1] = (srcRow[0]+srcRow[1])>>1;
   dst+=2; srcRow++;
  }
  dst[0] = dst[1] = dst[CIF_WIDTH*2] = dst[CIF_WIDTH*2+1] = srcRow[0];
  srcRow++; dst += 2; dst += CIF_WIDTH*2;

  for (y = 1; y < CIF_HEIGHT/2; y++) 
  {
    for (x = 1; x < CIF_WIDTH/2; x++) 
    {
      dst[0] = srcRow[0];
      dst[1] = (srcRow[0]+srcRow[1])>>1;
      dst[CIF_WIDTH] = (srcRow[0]+srcRow[CIF_WIDTH/2])>>1;
      dst[CIF_WIDTH+1] = (srcRow[0]+srcRow[1]+srcRow[CIF_WIDTH/2]+srcRow[CIF_WIDTH/2+1])>>2;
      dst+=2; srcRow++;
    }
    dst[0] = dst[1] = srcRow[0];
    dst[CIF_WIDTH] = dst[CIF_WIDTH+1] = (srcRow[0]+srcRow[CIF_WIDTH/2])>>1;
    srcRow++; dst += 2; dst += CIF_WIDTH;
  }
  for (x = 1; x < CIF_WIDTH/2; x++) 
  {
   dst[0] = dst[CIF_WIDTH] = srcRow[0];
   dst[1] = dst[CIF_WIDTH+1] = (srcRow[0]+srcRow[1])>>1;
   dst+=2; srcRow++;
  }
  dst[0] = dst[1] = dst[CIF_WIDTH] = dst[CIF_WIDTH+1] = srcRow[0];
  srcRow++; dst += 2; dst += CIF_WIDTH;

  for (y = 1; y < CIF_HEIGHT/2; y++) 
  {
    for (x = 1; x < CIF_WIDTH/2; x++) 
    {
      dst[0] = srcRow[0];
      dst[1] = (srcRow[0]+srcRow[1])>>1;
      dst[CIF_WIDTH] = (srcRow[0]+srcRow[CIF_WIDTH/2])>>1;
      dst[CIF_WIDTH+1] = (srcRow[0]+srcRow[1]+srcRow[CIF_WIDTH/2]+srcRow[CIF_WIDTH/2+1])>>2;
      dst+=2; srcRow++;
    }
    dst[0] = dst[1] = srcRow[0];
    dst[CIF_WIDTH] = dst[CIF_WIDTH+1] = (srcRow[0]+srcRow[CIF_WIDTH/2])>>1;
    srcRow++; dst += 2; dst += CIF_WIDTH;
  }
  for (x = 1; x < CIF_WIDTH/2; x++) 
  {
   dst[0] = dst[CIF_WIDTH] = srcRow[0];
   dst[1] = dst[CIF_WIDTH+1] = (srcRow[0]+srcRow[1])>>1;
   dst+=2; srcRow++;
  }
  dst[0] = dst[1] = dst[CIF_WIDTH] = dst[CIF_WIDTH+1] = srcRow[0];
  srcRow++; dst += 2; dst += CIF_WIDTH;
}

void MCUVideoMixer::ConvertCIF4ToCIF16(const void * _src, void * _dst)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst;

  //BYTE * dstEnd = dst + CIF_SIZE;
  int y, x;
  BYTE * srcRow;

  // copy Y
  srcRow = src;
  for (y = 1; y < CIF4_HEIGHT; y++) 
  {
    for (x = 1; x < CIF4_WIDTH; x++) 
    {
      dst[0] = srcRow[0];
      dst[1] = (srcRow[0]+srcRow[1])>>1;
      dst[CIF4_WIDTH*2] = (srcRow[0]+srcRow[CIF4_WIDTH])>>1;
      dst[CIF4_WIDTH*2+1] = (srcRow[0]+srcRow[1]+srcRow[CIF4_WIDTH]+srcRow[CIF4_WIDTH+1])>>2;
      dst+=2; srcRow++;
    }
    dst[0] = dst[1] = srcRow[0];
    dst[CIF4_WIDTH*2] = dst[CIF4_WIDTH*2+1] = (srcRow[0]+srcRow[CIF4_WIDTH])>>1;
    srcRow++; dst += 2; dst += CIF4_WIDTH*2;
  }
  for (x = 1; x < CIF4_WIDTH; x++) 
  {
   dst[0] = dst[CIF4_WIDTH*2] = srcRow[0];
   dst[1] = dst[CIF4_WIDTH*2+1] = (srcRow[0]+srcRow[1])>>1;
   dst+=2; srcRow++;
  }
  dst[0] = dst[1] = dst[CIF4_WIDTH*2] = dst[CIF4_WIDTH*2+1] = srcRow[0];
  srcRow++; dst += 2; dst += CIF4_WIDTH*2;

  for (y = 1; y < CIF4_HEIGHT/2; y++) 
  {
    for (x = 1; x < CIF4_WIDTH/2; x++) 
    {
      dst[0] = srcRow[0];
      dst[1] = (srcRow[0]+srcRow[1])>>1;
      dst[CIF4_WIDTH] = (srcRow[0]+srcRow[CIF4_WIDTH/2])>>1;
      dst[CIF4_WIDTH+1] = (srcRow[0]+srcRow[1]+srcRow[CIF4_WIDTH/2]+srcRow[CIF4_WIDTH/2+1])>>2;
      dst+=2; srcRow++;
    }
    dst[0] = dst[1] = srcRow[0];
    dst[CIF4_WIDTH] = dst[CIF4_WIDTH+1] = (srcRow[0]+srcRow[CIF4_WIDTH/2])>>1;
    srcRow++; dst += 2; dst += CIF4_WIDTH;
  }
  for (x = 1; x < CIF4_WIDTH/2; x++) 
  {
   dst[0] = dst[CIF4_WIDTH] = srcRow[0];
   dst[1] = dst[CIF4_WIDTH+1] = (srcRow[0]+srcRow[1])>>1;
   dst+=2; srcRow++;
  }
  dst[0] = dst[1] = dst[CIF4_WIDTH] = dst[CIF4_WIDTH+1] = srcRow[0];
  srcRow++; dst += 2; dst += CIF4_WIDTH;

  for (y = 1; y < CIF4_HEIGHT/2; y++) 
  {
    for (x = 1; x < CIF4_WIDTH/2; x++) 
    {
      dst[0] = srcRow[0];
      dst[1] = (srcRow[0]+srcRow[1])>>1;
      dst[CIF4_WIDTH] = (srcRow[0]+srcRow[CIF4_WIDTH/2])>>1;
      dst[CIF4_WIDTH+1] = (srcRow[0]+srcRow[1]+srcRow[CIF4_WIDTH/2]+srcRow[CIF4_WIDTH/2+1])>>2;
      dst+=2; srcRow++;
    }
    dst[0] = dst[1] = srcRow[0];
    dst[CIF4_WIDTH] = dst[CIF4_WIDTH+1] = (srcRow[0]+srcRow[CIF4_WIDTH/2])>>1;
    srcRow++; dst += 2; dst += CIF4_WIDTH;
  }
  for (x = 1; x < CIF4_WIDTH/2; x++) 
  {
   dst[0] = dst[CIF4_WIDTH] = srcRow[0];
   dst[1] = dst[CIF4_WIDTH+1] = (srcRow[0]+srcRow[1])>>1;
   dst+=2; srcRow++;
  }
  dst[0] = dst[1] = dst[CIF4_WIDTH] = dst[CIF4_WIDTH+1] = srcRow[0];
  srcRow++; dst += 2; dst += CIF4_WIDTH;
}

void MCUVideoMixer::Convert1To2(const void * _src, void * _dst, unsigned int w, unsigned int h)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst;

  if(w==QCIF_WIDTH && h==QCIF_HEIGHT) ConvertQCIFToCIF(_src,_dst);
  if(w==CIF_WIDTH && h==CIF_HEIGHT) ConvertCIFToCIF4(_src,_dst);
  if(w==CIF4_WIDTH && h==CIF4_HEIGHT) ConvertCIF4ToCIF16(_src,_dst);

  unsigned int y,x,w2=w*2;
  BYTE * srcRow;

  // copy Y
  srcRow = src;
  for (y = 1; y < h; y++) 
  {
    for (x = 1; x < w; x++) 
    {
      dst[0] = srcRow[0];
      dst[1] = (srcRow[0]+srcRow[1])>>1;
      dst[w2] = (srcRow[0]+srcRow[w])>>1;
      dst[w2+1] = (srcRow[0]+srcRow[1]+srcRow[w]+srcRow[w+1])>>2;
      dst+=2; srcRow++;
    }
    dst[0] = dst[1] = srcRow[0];
    dst[w2] = dst[w2+1] = (srcRow[0]+srcRow[w])>>1;
    srcRow++; dst += 2; dst += w2;
  }
  for (x = 1; x < w; x++) 
  {
   dst[0] = dst[w2] = srcRow[0];
   dst[1] = dst[w2+1] = (srcRow[0]+srcRow[1])>>1;
   dst+=2; srcRow++;
  }
  dst[0] = dst[1] = dst[w2] = dst[w2+1] = srcRow[0];
  srcRow++; dst += 2; dst += w2;

  w2=w>>1;
  for (y = 1; y < (h>>1); y++) 
  {
    for (x = 1; x < w2; x++) 
    {
      dst[0] = srcRow[0];
      dst[1] = (srcRow[0]+srcRow[1])>>1;
      dst[w] = (srcRow[0]+srcRow[w2])>>1;
      dst[w+1] = (srcRow[0]+srcRow[1]+srcRow[w2]+srcRow[w2+1])>>2;
      dst+=2; srcRow++;
    }
    dst[0] = dst[1] = srcRow[0];
    dst[w] = dst[w+1] = (srcRow[0]+srcRow[w2])>>1;
    srcRow++; dst += 2; dst += w;
  }
  for (x = 1; x < w2; x++) 
  {
   dst[0] = dst[w] = srcRow[0];
   dst[1] = dst[w+1] = (srcRow[0]+srcRow[1])>>1;
   dst+=2; srcRow++;
  }
  dst[0] = dst[1] = dst[w] = dst[w+1] = srcRow[0];
  srcRow++; dst += 2; dst += w;

  for (y = 1; y < (h>>1); y++) 
  {
    for (x = 1; x < w2; x++) 
    {
      dst[0] = srcRow[0];
      dst[1] = (srcRow[0]+srcRow[1])>>1;
      dst[w] = (srcRow[0]+srcRow[w2])>>1;
      dst[w+1] = (srcRow[0]+srcRow[1]+srcRow[w2]+srcRow[w2+1])>>2;
      dst+=2; srcRow++;
    }
    dst[0] = dst[1] = srcRow[0];
    dst[w] = dst[w+1] = (srcRow[0]+srcRow[w2])>>1;
    srcRow++; dst += 2; dst += w;
  }
  for (x = 1; x < w2; x++) 
  {
   dst[0] = dst[w] = srcRow[0];
   dst[1] = dst[w+1] = (srcRow[0]+srcRow[1])>>1;
   dst+=2; srcRow++;
  }
  dst[0] = dst[1] = dst[w] = dst[w+1] = srcRow[0];
  srcRow++; dst += 2; dst += w;
}


void MCUVideoMixer::ConvertCIFToTQCIF(const void * _src, void * _dst)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst;

  //BYTE * dstEnd = dst + CIF_SIZE;
  int y, x;
  unsigned int sum;
  BYTE * srcRow;

  // copy Y
  srcRow = src;
  for (y = 0; y < CIF_HEIGHT/2; y++) {
    for (x = 0; x < CIF_WIDTH/2; x++) {
      sum = *srcRow; dst[0] = *srcRow++;
      sum+= *srcRow; dst[2] = *srcRow++;
      dst[1] = (sum >> 1);
      dst += 3;
    }
    dst += CIF_WIDTH*3/2;
    for (x = 0; x < CIF_WIDTH/2; x++) {
      sum = *srcRow; dst[0] = *srcRow++;
      sum+= *srcRow; dst[2] = *srcRow++;
      dst[1] = (sum >> 1);
      dst += 3;
    }
    dst -= CIF_WIDTH*9/2;
    for (x = 0; x < CIF_WIDTH*3/2; x++) {
      sum = dst[0]+dst[CIF_WIDTH*3];
      dst[CIF_WIDTH*3/2] = (sum >> 1);
      dst++;
    }
   dst+=CIF_WIDTH*3;
  }

  // copy U
  for (y = 0; y < CIF_HEIGHT/4; y++) {
    for (x = 0; x < CIF_WIDTH/4; x++) {
      sum = *srcRow; dst[0] = *srcRow++;
      sum+= *srcRow; dst[2] = *srcRow++;
      dst[1] = (sum >> 1);
      dst += 3;
    }
    dst += CIF_WIDTH*3/4;
    for (x = 0; x < CIF_WIDTH/4; x++) {
      sum = *srcRow; dst[0] = *srcRow++;
      sum+= *srcRow; dst[2] = *srcRow++;
      dst[1] = (sum >> 1);
      dst += 3;
    }
    dst -= CIF_WIDTH*9/4;
    for (x = 0; x < CIF_WIDTH*3/4; x++) {
      sum = dst[0]+dst[CIF_WIDTH*3/2];
      dst[CIF_WIDTH*3/4] = (sum >> 1);
      dst++;
    }
   dst+=CIF_WIDTH*3/2;
  }

  // copy V
  for (y = 0; y < CIF_HEIGHT/4; y++) {
    for (x = 0; x < CIF_WIDTH/4; x++) {
      sum = *srcRow; dst[0] = *srcRow++;
      sum+= *srcRow; dst[2] = *srcRow++;
      dst[1] = (sum >> 1);
      dst += 3;
    }
    dst += CIF_WIDTH*3/4;
    for (x = 0; x < CIF_WIDTH/4; x++) {
      sum = *srcRow; dst[0] = *srcRow++;
      sum+= *srcRow; dst[2] = *srcRow++;
      dst[1] = (sum >> 1);
      dst += 3;
    }
    dst -= CIF_WIDTH*9/4;
    for (x = 0; x < CIF_WIDTH*3/4; x++) {
      sum = dst[0]+dst[CIF_WIDTH*3/2];
      dst[CIF_WIDTH*3/4] = (sum >> 1);
      dst++;
    }
   dst+=CIF_WIDTH*3/2;
  }
}

void MCUVideoMixer::ConvertCIF4ToTCIF(const void * _src, void * _dst)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst;

  //BYTE * dstEnd = dst + CIF_SIZE;
  int y, x;
  unsigned int sum;
  BYTE * srcRow;

  // copy Y
  srcRow = src;
  for (y = 0; y < CIF4_HEIGHT/2; y++) {
    for (x = 0; x < CIF4_WIDTH/2; x++) {
      sum = *srcRow; dst[0] = *srcRow++;
      sum+= *srcRow; dst[2] = *srcRow++;
      dst[1] = (sum >> 1);
      dst += 3;
    }
    dst += CIF4_WIDTH*3/2;
    for (x = 0; x < CIF4_WIDTH/2; x++) {
      sum = *srcRow; dst[0] = *srcRow++;
      sum+= *srcRow; dst[2] = *srcRow++;
      dst[1] = (sum >> 1);
      dst += 3;
    }
    dst -= CIF4_WIDTH*9/2;
    for (x = 0; x < CIF4_WIDTH*3/2; x++) {
      sum = dst[0]+dst[CIF4_WIDTH*3];
      dst[CIF4_WIDTH*3/2] = (sum >> 1);
      dst++;
    }
   dst+=CIF4_WIDTH*3;
  }

  // copy U
  for (y = 0; y < CIF4_HEIGHT/4; y++) {
    for (x = 0; x < CIF4_WIDTH/4; x++) {
      sum = *srcRow; dst[0] = *srcRow++;
      sum+= *srcRow; dst[2] = *srcRow++;
      dst[1] = (sum >> 1);
      dst += 3;
    }
    dst += CIF4_WIDTH*3/4;
    for (x = 0; x < CIF4_WIDTH/4; x++) {
      sum = *srcRow; dst[0] = *srcRow++;
      sum+= *srcRow; dst[2] = *srcRow++;
      dst[1] = (sum >> 1);
      dst += 3;
    }
    dst -= CIF4_WIDTH*9/4;
    for (x = 0; x < CIF4_WIDTH*3/4; x++) {
      sum = dst[0]+dst[CIF4_WIDTH*3/2];
      dst[CIF4_WIDTH*3/4] = (sum >> 1);
      dst++;
    }
   dst+=CIF4_WIDTH*3/2;
  }

  // copy V
  for (y = 0; y < CIF4_HEIGHT/4; y++) {
    for (x = 0; x < CIF4_WIDTH/4; x++) {
      sum = *srcRow; dst[0] = *srcRow++;
      sum+= *srcRow; dst[2] = *srcRow++;
      dst[1] = (sum >> 1);
      dst += 3;
    }
    dst += CIF4_WIDTH*3/4;
    for (x = 0; x < CIF4_WIDTH/4; x++) {
      sum = *srcRow; dst[0] = *srcRow++;
      sum+= *srcRow; dst[2] = *srcRow++;
      dst[1] = (sum >> 1);
      dst += 3;
    }
    dst -= CIF4_WIDTH*9/4;
    for (x = 0; x < CIF4_WIDTH*3/4; x++) {
      sum = dst[0]+dst[CIF4_WIDTH*3/2];
      dst[CIF4_WIDTH*3/4] = (sum >> 1);
      dst++;
    }
   dst+=CIF4_WIDTH*3/2;
  }
}

void MCUVideoMixer::ConvertQCIFToCIF4(const void * _src, void * _dst)
{
  BYTE * src = (BYTE *)_src;
  BYTE * dst = (BYTE *)_dst;

  //BYTE * dstEnd = dst + CIF_SIZE;
  int y, x;
  BYTE * srcRow;

  // copy Y
  for (y = 0; y < QCIF_HEIGHT; y++) {
    srcRow = src;
    for (x = 0; x < QCIF_WIDTH; x++) {
      dst[0] = dst[1] = dst[2] = dst[3] = *srcRow++;
      dst += 4;
    }
    srcRow = src;
    for (x = 0; x < QCIF_WIDTH; x++) {
      dst[0] = dst[1] = dst[2] = dst[3] = *srcRow++;
      dst += 4;
    }
    srcRow = src;
    for (x = 0; x < QCIF_WIDTH; x++) {
      dst[0] = dst[1] = dst[2] = dst[3] = *srcRow++;
      dst += 4;
    }
    srcRow = src;
    for (x = 0; x < QCIF_WIDTH; x++) {
      dst[0] = dst[1] = dst[2] = dst[3] = *srcRow++;
      dst += 4;
    }
    src += QCIF_WIDTH;
  }

  // copy U
  for (y = 0; y < QCIF_HEIGHT/2; y++) {
    srcRow = src;
    for (x = 0; x < QCIF_WIDTH/2; x++) {
      dst[0] = dst[1] = dst[2] = dst[3] = *srcRow++;
      dst += 4;
    }
    srcRow = src;
    for (x = 0; x < QCIF_WIDTH/2; x++) {
      dst[0] = dst[1] = dst[2] = dst[3] = *srcRow++;
      dst += 4;
    }
    srcRow = src;
    for (x = 0; x < QCIF_WIDTH/2; x++) {
      dst[0] = dst[1] = dst[2] = dst[3] = *srcRow++;
      dst += 4;
    }
    srcRow = src;
    for (x = 0; x < QCIF_WIDTH/2; x++) {
      dst[0] = dst[1] = dst[2] = dst[3] = *srcRow++;
      dst += 4;
    }
    src += QCIF_WIDTH/2;
  }

  // copy V
  for (y = 0; y < QCIF_HEIGHT/2; y++) {
    srcRow = src;
    for (x = 0; x < QCIF_WIDTH/2; x++) {
      dst[0] = dst[1] = dst[2] = dst[3] = *srcRow++;
      dst += 4;
    }
    srcRow = src;
    for (x = 0; x < QCIF_WIDTH/2; x++) {
      dst[0] = dst[1] = dst[2] = dst[3] = *srcRow++;
      dst += 4;
    }
    srcRow = src;
    for (x = 0; x < QCIF_WIDTH/2; x++) {
      dst[0] = dst[1] = dst[2] = dst[3] = *srcRow++;
      dst += 4;
    }
    srcRow = src;
    for (x = 0; x < QCIF_WIDTH/2; x++) {
      dst[0] = dst[1] = dst[2] = dst[3] = *srcRow++;
      dst += 4;
    }
    src += QCIF_WIDTH/2;
  }
}
#endif // #if USE_LIBYUV==0

void MCUVideoMixer::VideoSplitLines(void * dst, VideoMixPosition & vmp, unsigned int fw, unsigned int fh){
 unsigned int i;
 BYTE * d = (BYTE *)dst;
 for(i=1;i<fh-1;i++){
  if(d[i*fw]>127)d[i*fw]=255;else if(d[i*fw]<63)d[i*fw]=64; else d[i*fw]<<=1;
  d[i*fw+fw-1]>>=1;
 }
 for(i=1;i<fw-1;i++){
  if(d[i]>127)d[i]=255;else if(d[i]<63)d[i]=64; else d[i]<<=1;
  d[(fh-1)*fw+i]>>=1;
 }
 if(d[0]>127)d[0]=255;else if(d[0]<63)d[0]=64; else d[0]<<=1;
 d[fw-1]>>=2;
 d[(fh-1)*fw]>>=2;
 d[(fh-1)*fw+fw-1]>>=1;
 return;
}


///////////////////////////////////////////////////////////////////////////////////////

MCUSimpleVideoMixer::MCUSimpleVideoMixer(BOOL _forceScreenSplit)
//  : forceScreenSplit(_forceScreenSplit)
{
  forceScreenSplit = _forceScreenSplit;
  VMPListInit();
  frameStores.AddFrameStore(CIF4_WIDTH, CIF4_HEIGHT);
  imageStore_size=0;
  imageStore1_size=0;
  imageStore2_size=0;
#if USE_LIBJPEG
  jpegTime=0;
  jpegSize=0;
#endif
//  imageStore.SetSize(CIF16_SIZE);
//  imageStore1.SetSize(CIF16_SIZE);
//  imageStore2.SetSize(CIF16_SIZE);

  converter = PColourConverter::Create("YUV420P", "YUV420P", CIF16_WIDTH, CIF16_HEIGHT);
  specialLayout = 0;
}

BOOL MCUSimpleVideoMixer::ReadFrame(ConferenceMember &, void * buffer, int width, int height, PINDEX & amount)
{
  PWaitAndSignal m(mutex);

  // special case of one member means fill with black
//  if (!forceScreenSplit && rows == 0) {
  if (!forceScreenSplit && vmpNum <= 1) {
    VideoFrameStoreList::FrameStore & fs = frameStores.GetFrameStore(width, height);
    if (!fs.valid) {
      if (!OpenMCU::Current().GetPreMediaFrame(fs.data.GetPointer(), width, height, amount))
        FillYUVFrame(fs.data.GetPointer(), 0, 0, 0, width, height);
      fs.valid = TRUE;
      fs.used = 300;
    }
    memcpy(buffer, fs.data.GetPointer(), amount);
  }

  // special case of two members means we do nothing, and tell caller to look for full screen version of other video
//  if (!forceScreenSplit && rows == 1) 
  if (!forceScreenSplit && vmpNum == 2)
    return FALSE;

  return ReadMixedFrame(buffer, width, height, amount);
}

BOOL MCUSimpleVideoMixer::ReadSrcFrame(VideoFrameStoreList & srcFrameStores, void * buffer, int width, int height, PINDEX & amount)
{
  PWaitAndSignal m(mutex);

  VideoFrameStoreList::FrameStore & Fs = srcFrameStores.GetFrameStore(width, height);

  if (!Fs.valid) 
  {
   if(width*CIF_HEIGHT!=height*CIF_WIDTH || 
      (width!=CIF_WIDTH && width!=CIF4_WIDTH && width!=CIF16_WIDTH)) // non standart frame
//eg width=16; height=9; 16*9>9*11=TRUE; nw=9*11/9=11;
   {
    int nw,nh;
    if(width*CIF_HEIGHT == height*CIF_WIDTH) { nw=width; nh=height; }
    else if(width*CIF_HEIGHT > height*CIF_WIDTH) // needs h cut
    { nw=(height*CIF_WIDTH)/CIF_HEIGHT; nh=height; }
    else { nw=width; nh=(width*CIF_HEIGHT)/CIF_WIDTH; }

    if(nw <= CIF_WIDTH*1.5)
    {
     VideoFrameStoreList::FrameStore & cifFs = srcFrameStores.GetFrameStore(CIF_WIDTH, CIF_HEIGHT);
     cifFs.used=300;
     if(cifFs.valid)
     {
      imageStores_operational_size(nw,nh,_IMGST2);
//      ConvertFRAMEToCUSTOM_FRAME(cifFs.data.GetPointer(),imageStore2.GetPointer(), CIF_WIDTH, CIF_HEIGHT,nw,nh);
      ResizeYUV420P(cifFs.data.GetPointer(),imageStore2.GetPointer(), CIF_WIDTH, CIF_HEIGHT,nw,nh);
      CopyRectIntoFrame(imageStore2.GetPointer(),Fs.data.GetPointer(),(width-nw)>>1,(height-nh)>>1,nw,nh,width,height);
      Fs.valid=1;
     } 
    }
    else if(nw < CIF4_WIDTH+100)
    {
     VideoFrameStoreList::FrameStore & cif4Fs = srcFrameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT);
     cif4Fs.used=300;
     if(cif4Fs.valid)
     {
      imageStores_operational_size(nw,nh,_IMGST2);
//      ConvertFRAMEToCUSTOM_FRAME(cif4Fs.data.GetPointer(),imageStore2.GetPointer(), CIF4_WIDTH, CIF4_HEIGHT,nw,nh);
      ResizeYUV420P(cif4Fs.data.GetPointer(),imageStore2.GetPointer(), CIF4_WIDTH, CIF4_HEIGHT,nw,nh);
      CopyRectIntoFrame(imageStore2.GetPointer(),Fs.data.GetPointer(),(width-nw)>>1,(height-nh)>>1,nw,nh,width,height);
//      nw = CIF4_WIDTH; nh = CIF4_HEIGHT;
//      CopyRectIntoFrame(cif4Fs.data.GetPointer(),Fs.data.GetPointer(),(width-nw)>>1,(height-nh)>>1,nw,nh,width,height);
      Fs.valid=1;
//      cout << "Read\n";
     } 
    }
    else
    {
     VideoFrameStoreList::FrameStore & cif16Fs = srcFrameStores.GetFrameStore(CIF16_WIDTH, CIF16_HEIGHT);
     cif16Fs.used=300;
     if(cif16Fs.valid)
     {
      imageStores_operational_size(nw,nh,_IMGST2);
//      ConvertFRAMEToCUSTOM_FRAME(cif16Fs.data.GetPointer(),imageStore2.GetPointer(), CIF16_WIDTH, CIF16_HEIGHT,nw,nh);
      ResizeYUV420P(cif16Fs.data.GetPointer(),imageStore2.GetPointer(), CIF16_WIDTH, CIF16_HEIGHT,nw,nh);
      CopyRectIntoFrame(imageStore2.GetPointer(),Fs.data.GetPointer(),(width-nw)>>1,(height-nh)>>1,nw,nh,width,height);
      Fs.valid=1;
     } 
    } 
   }
  }

  if (!Fs.valid) 
  {
   if (!OpenMCU::Current().GetPreMediaFrame(Fs.data.GetPointer(), width, height, amount))
        MCUVideoMixer::FillYUVFrame(Fs.data.GetPointer(), 0, 0, 0, width, height);
    Fs.valid = TRUE;
  }
  memcpy(buffer, Fs.data.GetPointer(), amount);
  Fs.used=300;

  return TRUE;
}



BOOL MCUSimpleVideoMixer::ReadMixedFrame(void * buffer, int width, int height, PINDEX & amount)
{
  return ReadSrcFrame(frameStores, buffer, width, height, amount);
}


BOOL MCUSimpleVideoMixer::WriteFrame(ConferenceMemberId id, const void * buffer, int width, int height, PINDEX amount)
{
  PWaitAndSignal m(mutex);

  // special case of one member means we do nothing, and don't bother looking for other user to copy from
//  if (!forceScreenSplit && rows == 0) 
  if (!forceScreenSplit && vmpNum <= 1) 
    return TRUE;

  // special case of two members means we do nothing, and tell caller to look for another frame to write to
//  if (!forceScreenSplit && rows == 1) 
  if (!forceScreenSplit && vmpNum == 2) 
    return FALSE;

  // write data into sub frame of mixed image
  VideoMixPosition *pVMP = VMPListFindVMP(id);
  if(pVMP==NULL) return TRUE;

  WriteSubFrame(*pVMP, buffer, width, height, amount);

  return TRUE;
}

void MCUSimpleVideoMixer::CalcVideoSplitSize(unsigned int imageCount, int & subImageWidth, int & subImageHeight, int & cols, int & rows)
{
//  int specials[25]={0,9,16,16,36,2,9,16,25,12,16,16,0,0,0,0,0,0,0,0,0,2,2,2,2};
//  if(specialLayout>0)imageCount=specials[specialLayout];
  if (!forceScreenSplit && imageCount < 2) {
    subImageWidth  = CIF4_WIDTH;
    subImageHeight = CIF4_HEIGHT;
    cols           = 0;
    rows           = 0;
  }
  else
  if (!forceScreenSplit && imageCount == 2) {
    subImageWidth  = CIF4_WIDTH;
    subImageHeight = CIF4_HEIGHT;
    cols           = 1;
    rows           = 1;
  }
  else
  if (imageCount == 1) {
    subImageWidth  = CIF4_WIDTH;
    subImageHeight = CIF4_HEIGHT;
    cols           = 1;
    rows           = 1;
  }
  else
  if (imageCount == 2) {
    subImageWidth  = CIF_WIDTH;
    subImageHeight = CIF_HEIGHT;
    cols           = 2;
    rows           = 1;
  }
  else
  if (imageCount <= 4) {
    subImageWidth  = CIF_WIDTH;
    subImageHeight = CIF_HEIGHT;
    cols           = 2;
    rows           = 2;
  }
  else
  if (imageCount <= 9) {
    subImageWidth  = Q3CIF_WIDTH;
    subImageHeight = Q3CIF_HEIGHT;
    cols           = 3;
    rows           = 3;
  }
  else if (imageCount <= 12) {
    subImageWidth  = QCIF_WIDTH;
    subImageHeight = Q3CIF_HEIGHT;
    cols           = 4;
    rows           = 3;
  }
  else if (imageCount <= 16) {
    subImageWidth  = QCIF_WIDTH;
    subImageHeight = QCIF_HEIGHT;
    cols           = 4;
    rows           = 4;
  }
  else if (imageCount <= 25) {
    subImageWidth  = SQ5CIF_WIDTH;
    subImageHeight = SQ5CIF_HEIGHT;
    cols           = 5;
    rows           = 5;
  }
  else if (imageCount <= 36) {
    subImageWidth  = SQ3CIF_WIDTH;
    subImageHeight = SQ3CIF_HEIGHT;
    cols           = 6;
    rows           = 6;
  }
}

void MCUSimpleVideoMixer::MyCalcVideoSplitSize(unsigned int imageCount, int *subImageWidth, int *subImageHeight, int *cols, int *rows)
{
//                  0 1  2  3  4 5 6  7  8  9 10 11    14  16  18  20
  int specials[25]={0,9,16,16,36,2,9,16,25,12,16,16,0,0,0,0,0,0,0,0,0,2,2,2,2};
  if(specialLayout>0)imageCount=specials[specialLayout];
  if (imageCount < 2) {
    *subImageWidth  = CIF4_WIDTH;
    *subImageHeight = CIF4_HEIGHT;
    *cols           = 1;
    *rows           = 1;
  }
  else
  if (imageCount == 2) {
    *subImageWidth  = CIF_WIDTH;
    *subImageHeight = CIF_HEIGHT;
    *cols           = 2;
    *rows           = 1;
  }
  else
  if (imageCount <= 4) {
    *subImageWidth  = CIF_WIDTH;
    *subImageHeight = CIF_HEIGHT;
    *cols           = 2;
    *rows           = 2;
  }
  else
  if (imageCount <= 9) {
    *subImageWidth  = Q3CIF_WIDTH;
    *subImageHeight = Q3CIF_HEIGHT;
    *cols           = 3;
    *rows           = 3;
  }
  else
  if (imageCount <= 12) {
    *subImageWidth  = QCIF_WIDTH;
    *subImageHeight = Q3CIF_HEIGHT;
    *cols           = 4;
    *rows           = 3;
  }
  else if (imageCount <= 16) {
    *subImageWidth  = QCIF_WIDTH;
    *subImageHeight = QCIF_HEIGHT;
    *cols           = 4;
    *rows           = 4;
  }
  else if (imageCount <= 25) {
    *subImageWidth  = SQ5CIF_WIDTH;
    *subImageHeight = SQ5CIF_HEIGHT;
    *cols           = 5;
    *rows           = 5;
  }
  else if (imageCount <= 36) {
    *subImageWidth  = SQ3CIF_WIDTH;
    *subImageHeight = SQ3CIF_HEIGHT;
    *cols           = 6;
    *rows           = 6;
  }
}

void MCUSimpleVideoMixer::ReallocatePositions()
{
/*
  VideoFrameStoreList::FrameStore & cifFs = frameStores.GetFrameStore(CIF_WIDTH, CIF_HEIGHT);
  VideoFrameStoreList::FrameStore & cif16Fs = frameStores.GetFrameStore(CIF16_WIDTH, CIF16_HEIGHT);
  FillCIF4YUVFrame(frameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT).data.GetPointer(), 0, 0, 0);
  FillCIFYUVFrame(cifFs.data.GetPointer(), 0, 0, 0);
  FillCIF16YUVFrame(cif16Fs.data.GetPointer(), 0, 0, 0);
  frameStores.InvalidateExcept(CIF4_WIDTH, CIF4_HEIGHT);
  cifFs.valid = 1;
  cif16Fs.valid = 1;
*/
  NullAllFrameStores();
  VideoMixPosition *r;
  unsigned int i = 0;
  r = vmpList->next;
  while(r != NULL)
  {
   VideoMixPosition & vmp = *r;
   vmp.n      = i;
   vmp.label_init = FALSE;
   vmp.fc     = 0;
   vmp.xpos=OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[i].posx;
   vmp.ypos=OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[i].posy;
   vmp.width=OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[i].width;
   vmp.height=OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[i].height;
   ++i;
   r = r->next;
  }
}

BOOL MCUSimpleVideoMixer::AddVideoSource(ConferenceMemberId id, ConferenceMember & mbr)
{
  PWaitAndSignal m(mutex);

  if (vmpNum == MAX_SUBFRAMES)
  {
   cout << "AddVideoSource " << id << " " << vmpNum << " maximum exceeded (" << MAX_SUBFRAMES << ")\n";
   return FALSE;
  }

  // make sure this source is not already in the list
  VideoMixPosition *newPosition = VMPListFindVMP(id); if(newPosition != NULL)
  {
    cout << "AddVideoSource " << id << " " << vmpNum << " already in list (" << newPosition << ")\n";
    return TRUE;
  }

// finding best matching layout (newsL):
  int newsL=-1; int maxL=-1; unsigned maxV=99999;
  for(unsigned i=0;i<OpenMCU::vmcfg.vmconfs;i++) if(OpenMCU::vmcfg.vmconf[i].splitcfg.mode_mask&1)
  {
    if(OpenMCU::vmcfg.vmconf[i].splitcfg.vidnum==vmpNum+1) { newsL=i; break; }
    else if((OpenMCU::vmcfg.vmconf[i].splitcfg.vidnum>vmpNum)&&(OpenMCU::vmcfg.vmconf[i].splitcfg.vidnum<maxV))
    {
     maxV=OpenMCU::vmcfg.vmconf[i].splitcfg.vidnum;
     maxL=(int)i;
    }
  }
  if(newsL==-1) {
    if(maxL!=-1) newsL=maxL; else newsL=specialLayout;
  }

  if ((newsL != specialLayout)||(vmpNum==0)) // split changed or first vmp
  {
    specialLayout=newsL;
    newPosition = CreateVideoMixPosition(id, OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmpNum].posx, OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmpNum].posy, OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmpNum].width, OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmpNum].height);
    newPosition->type=2;
    newPosition->n=vmpNum;
    newPosition->label_init=FALSE;
    if(OpenMCU::vmcfg.vmconf[newsL].splitcfg.new_from_begin) VMPListInsVMP(newPosition); else VMPListAddVMP(newPosition);
    cout << "AddVideoSource " << id << " " << vmpNum << " done (" << newPosition << ")\n";
    ReallocatePositions();
  }
  else  // otherwise find an empty position
  {
    for(unsigned i=0;i<OpenMCU::vmcfg.vmconf[newsL].splitcfg.vidnum;i++)
    {
      newPosition = vmpList->next;
      while (newPosition != NULL) { if (newPosition->n != (int)i) newPosition=newPosition->next; else break; }
      if(newPosition==NULL) // empty position found
      {
        newPosition = CreateVideoMixPosition(id, OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[i].posx, OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[i].posy, OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[i].width, OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[i].height);
        newPosition->n=i;
        newPosition->type=2;
        newPosition->label_init=FALSE;
        if(OpenMCU::vmcfg.vmconf[specialLayout].splitcfg.new_from_begin) VMPListInsVMP(newPosition); else VMPListAddVMP(newPosition);
        cout << "AddVideoSource " << id << " " << vmpNum << " added as " << i << " (" << newPosition << ")\n";
        break;
      }
    }
  }

  if (newPosition != NULL) {
    PBYTEArray fs(CIF4_SIZE);
    int amount = newPosition->width*newPosition->height*3/2;
    if (!OpenMCU::Current().GetPreMediaFrame(fs.GetPointer(), newPosition->width, newPosition->height, amount))
      FillYUVFrame(fs.GetPointer(), 0, 0, 0, newPosition->width, newPosition->height);
    WriteSubFrame(*newPosition, fs.GetPointer(), newPosition->width, newPosition->height, amount);
  }
  else cout << "AddVideoSource " << id << " " << vmpNum << " could not find empty video position";

  return TRUE;
}

void MCUSimpleVideoMixer::RemoveVideoSource(ConferenceMemberId id, ConferenceMember & mbr)
{
  PWaitAndSignal m(mutex);

  // make sure this source is in the list
  {
    VideoMixPosition *pVMP = VMPListFindVMP(id);
    if(pVMP == NULL) return;

    // clear the position where the frame was
    VideoMixPosition & vmp = *pVMP;
    VideoFrameStoreList::FrameStore & cifFs = frameStores.GetFrameStore(CIF_WIDTH, CIF_HEIGHT);
    VideoFrameStoreList::FrameStore & cif16Fs = frameStores.GetFrameStore(CIF16_WIDTH, CIF16_HEIGHT);
//    PINDEX retsz;
    if (vmpNum == 1)
    {
/*     if(!OpenMCU::Current().GetPreMediaFrame(frameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT).data.GetPointer(), CIF4_WIDTH, CIF4_HEIGHT, retsz))
      FillCIF4YUVFrame(frameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT).data.GetPointer(), 0, 0, 0);
     if(!OpenMCU::Current().GetPreMediaFrame(cifFs.data.GetPointer(), CIF_WIDTH, CIF_HEIGHT, retsz))
      FillCIFYUVFrame(cifFs.data.GetPointer(), 0, 0, 0);
     if(!OpenMCU::Current().GetPreMediaFrame(cif16Fs.data.GetPointer(), CIF16_WIDTH, CIF16_HEIGHT, retsz))
      FillCIF16YUVFrame(cif16Fs.data.GetPointer(), 0, 0, 0);
*/    NullAllFrameStores();
    }
    else
    {
/*      FillCIF4YUVRect(frameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT).data.GetPointer(), 0, 0, 0, vmp.xpos, vmp.ypos, vmp.width, vmp.height);
      FillCIFYUVRect(cifFs.data.GetPointer(), 0, 0, 0, vmp.xpos/2, vmp.ypos/2, vmp.width/2, vmp.height/2);
      FillCIF16YUVRect(cif16Fs.data.GetPointer(), 0, 0, 0, vmp.xpos*2, vmp.ypos*2, vmp.width*2, vmp.height*2);
*/
      NullRectangle(vmp.xpos,vmp.ypos,vmp.width,vmp.height);
    }  
    frameStores.InvalidateExcept(CIF4_WIDTH, CIF4_HEIGHT);
    cifFs.valid = 1;
    cif16Fs.valid = 1;

    // remove the source from the list
    VMPListDelVMP(pVMP);

    // erase the video position information
    delete pVMP;
  }

  // calculate the kind of video split we need to include the new source
/*
  int newSubImageWidth, newSubImageHeight, newCols, newRows; 
  CalcVideoSplitSize((unsigned int)vmpNum, newSubImageWidth, newSubImageHeight, newCols, newRows);
*/
  int newsL=-1; int maxL=-1; unsigned maxV=99999;
  for(unsigned i=0;i<OpenMCU::vmcfg.vmconfs;i++) if(OpenMCU::vmcfg.vmconf[i].splitcfg.mode_mask&1)
  {
    if(OpenMCU::vmcfg.vmconf[i].splitcfg.vidnum==vmpNum) { newsL=i; break; }
    else if((OpenMCU::vmcfg.vmconf[i].splitcfg.vidnum>vmpNum)&&(OpenMCU::vmcfg.vmconf[i].splitcfg.vidnum<maxV))
    { maxV=OpenMCU::vmcfg.vmconf[i].splitcfg.vidnum; maxL=i; }
  }
  if(newsL==-1) {
    if(maxL!=-1) newsL=maxL; else newsL=specialLayout;
  }



  // if the subimage size has changed, rearrange everything
/*  if (newSubImageWidth != subImageWidth || newSubImageHeight != subImageHeight || newRows != rows || newCols != cols) {
    rows           = newRows;
    cols           = newCols;
    subImageWidth  = newSubImageWidth;
    subImageHeight = newSubImageHeight;
*/
  if (newsL!=specialLayout || OpenMCU::vmcfg.vmconf[newsL].splitcfg.reallocate_on_disconnect) {
    specialLayout=newsL;
    ReallocatePositions();
  }
}

int MCUSimpleVideoMixer::GetPositionSet()
{
  return specialLayout;
/*
  if(specialLayout==1) return 100;
  if(specialLayout==2) return 121;
  if(specialLayout==3) return 144;
  if(specialLayout==4) return 900;
  if(specialLayout==5) return 400;
  if(specialLayout==6) return 961;
  if(specialLayout==7) return 1024;
  if(specialLayout==8) return 169;
  if(specialLayout==9) return 196;
  if(specialLayout==10) return 256;
  if(specialLayout==11) return 289;
  if(specialLayout==21) return 441;
  if(specialLayout==22) return 484;
  if(specialLayout==23) return 529;
  if(specialLayout==24) return 576;
  return cols*rows;
*/
}

int MCUSimpleVideoMixer::GetPositionNum(ConferenceMemberId id)
{
  PWaitAndSignal m(mutex);

  VideoMixPosition *pVMP = VMPListFindVMP(id);
  if(pVMP == NULL) return -1;

//  return pVMP->ypos/subImageHeight*cols+pVMP->xpos/subImageWidth;
  return pVMP->n;
}

int MCUSimpleVideoMixer::GetPositionStatus(ConferenceMemberId id)
{
  PWaitAndSignal m(mutex);

  VideoMixPosition *pVMP = VMPListFindVMP(id);
  if(pVMP == NULL) return -1;

  return pVMP->status;
}

int MCUSimpleVideoMixer::GetPositionType(ConferenceMemberId id)
{
  PWaitAndSignal m(mutex);

  VideoMixPosition *pVMP = VMPListFindVMP(id);
  if(pVMP == NULL) return -1;

  return pVMP->type;
}

void MCUSimpleVideoMixer::SetPositionStatus(ConferenceMemberId id,int newStatus)
{
  PWaitAndSignal m(mutex);

  VideoMixPosition *pVMP = VMPListFindVMP(id);
  if(pVMP == NULL) return;

  pVMP->status=newStatus;
}

ConferenceMemberId MCUSimpleVideoMixer::GetPositionId(int pos)
{
  PWaitAndSignal m(mutex);

  VideoMixPosition *r = vmpList->next;
  while(r!=NULL)
  {
   VideoMixPosition & vmp = *r;
//    if (vmp.ypos/subImageHeight*cols+vmp.xpos/subImageWidth == pos ){
    if (vmp.n == pos ){
     if(vmp.type>1) return (void *)(1-vmp.type); else return vmp.id;
    }
   r=r->next;
  }

  return NULL;
}

ConferenceMemberId MCUSimpleVideoMixer::SetVADPosition(ConferenceMemberId id, int chosenVan, unsigned short timeout)
{
 int maxStatus=0;
 ConferenceMemberId maxId=(void *)(-1);
 VideoMixPosition *VADvmp=NULL;
 
  PWaitAndSignal m(mutex);
  
  VideoMixPosition *r = vmpList->next;
  while(r != NULL)
  {
   VideoMixPosition & vmp = *r;
//    if(vmp.id==(void *)(-1)) { maxStatus=600; VADvmp = r->second; break; }
   if(vmp.chosenVan!=0) { r=r->next; continue; } // don`n consider chosenVan
   if((long)vmp.id>=0 && (long)vmp.id<100) { maxId=vmp.id; maxStatus=timeout; VADvmp = r; break; }
   if(vmp.type==2 && vmp.status>maxStatus) { maxId=vmp.id; maxStatus=vmp.status; VADvmp = r; }
   if(vmp.type==3 && chosenVan==1 && vmp.status>maxStatus) { maxId=vmp.id; maxStatus=vmp.status; VADvmp = r; }
   r=r->next;
  }

  if(maxId==(void *)(-1)) return NULL;
  if(maxStatus < timeout && chosenVan==0) return NULL;
  VADvmp->id=id; VADvmp->status=0; VADvmp->chosenVan=chosenVan;
  frameStores.InvalidateExcept(CIF4_WIDTH, CIF4_HEIGHT);
  
  cout << "SetVADPosition\n";
  if(maxId==NULL) return (void *)1;
  return maxId;
}

BOOL MCUSimpleVideoMixer::SetVAD2Position(ConferenceMemberId id)
{
 int maxStatus=0;
// ConferenceMemberId maxId;
 VideoMixPosition *VAD2vmp=NULL;
 
  PWaitAndSignal m(mutex);

  if(GetPositionType(id)!=2) return FALSE;

  VideoMixPosition *r = vmpList->next;
 ConferenceMemberId maxId=r->id; // tried to remove warning "'maxId' may be used uninitialized in this function" (kay27)
  while(r != NULL)
  {
   VideoMixPosition & vmp = *r;
   if(vmp.type==3 && (long)vmp.id>=0 && (long)vmp.id<100) { maxId=vmp.id; maxStatus=6000; VAD2vmp = r; break; }
   if(vmp.type==3 && vmp.status>maxStatus) { maxId=vmp.id; maxStatus=vmp.status; VAD2vmp = r; }
   r = r->next;
  }

  if(maxStatus < 3000) return FALSE;
  if(id==maxId) { cout << "Bad VAD2 switch\n"; VAD2vmp->status=0; return FALSE; }
  VideoMixPosition *oldVMP = VMPListFindVMP(id);
  if(oldVMP==NULL) return FALSE;
  int pos = GetPositionNum(id);
  int cv = VAD2vmp->chosenVan;
  VAD2vmp->id=id; VAD2vmp->status=0; VAD2vmp->chosenVan=oldVMP->chosenVan;
  if((long)maxId>=0 && (long)maxId<100) maxId=(ConferenceMemberId)pos;
  oldVMP->id=maxId; oldVMP->status=0; oldVMP->chosenVan=cv;
  
  VideoFrameStoreList::FrameStore & cifFs = frameStores.GetFrameStore(CIF_WIDTH, CIF_HEIGHT);
  VideoFrameStoreList::FrameStore & cif16Fs = frameStores.GetFrameStore(CIF16_WIDTH, CIF16_HEIGHT);
  if((long)maxId>=0 && (long)maxId<100)
  {
/*   FillCIF4YUVRect(frameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT).data.GetPointer(), 0, 0, 0, oldVMP->xpos, oldVMP->ypos, oldVMP->width, oldVMP->height);
   FillCIFYUVRect(cifFs.data.GetPointer(), 0, 0, 0, oldVMP->xpos/2, oldVMP->ypos/2, oldVMP->width/2, oldVMP->height/2);
   FillCIF16YUVRect(cif16Fs.data.GetPointer(), 0, 0, 0, oldVMP->xpos*2, oldVMP->ypos*2, oldVMP->width*2, oldVMP->height*2);
*/
    NullRectangle(oldVMP->xpos,oldVMP->ypos,oldVMP->width,oldVMP->height);
  }
  frameStores.InvalidateExcept(CIF4_WIDTH, CIF4_HEIGHT);
  cifFs.valid = 1;
  cif16Fs.valid = 1;
 
  cout << "SetVAD2Position\n";
  return TRUE;
}

BOOL MCUSimpleVideoMixer::MyAddVideoSource(int num, ConferenceMemberId *idp)
{
  PWaitAndSignal m(mutex);

  specialLayout = num; // We have to set 'num' properly for MyCalcVideoSplitSize first:

  VideoMixPosition * newPosition = NULL;

  unsigned i;
  int x,y,w,h;
  for(i=0;i<OpenMCU::vmcfg.vmconf[specialLayout].splitcfg.vidnum;i++)
  {
   if(idp[i]==NULL) continue;
   x=OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[i].posx;
   y=OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[i].posy;
   w=OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[i].width;
   h=OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[i].height;

   newPosition = CreateVideoMixPosition(idp[i], x, y, w, h);
   newPosition->n=i;

   if(idp[i]==(void *)(-1)) 
    { newPosition->type=2; newPosition->id=(void *)i; cout << "Here\n"; } // new vad position
   else if(idp[i]==(void *)(-2)) 
    { newPosition->type=3; newPosition->id=(void *)i; } // new vad2 position
   else newPosition->type=1; // static position
   newPosition->label_init=FALSE;
//   VMPListInsVMP(newPosition);
   if(OpenMCU::vmcfg.vmconf[specialLayout].splitcfg.new_from_begin) VMPListInsVMP(newPosition); else VMPListAddVMP(newPosition);
/// was commented since at least p35. let's uncomment and test:
   if (newPosition != NULL) {
     PBYTEArray fs(CIF4_SIZE);
     int amount = newPosition->width*newPosition->height*3/2;
     if (!OpenMCU::Current().GetPreMediaFrame(fs.GetPointer(), newPosition->width, newPosition->height, amount))
       FillYUVFrame(fs.GetPointer(), 0, 0, 0, newPosition->width, newPosition->height);
     WriteSubFrame(*newPosition, fs.GetPointer(), newPosition->width, newPosition->height, amount);
   }
  }

  return TRUE;
}

void MCUSimpleVideoMixer::MyRemoveVideoSource(int pos, BOOL flag)
{
  PWaitAndSignal m(mutex);

  VideoMixPosition *r = vmpList->next;
  while(r != NULL)
  {
   VideoMixPosition & vmp = *r;
//   if (vmp.ypos/subImageHeight*cols+vmp.xpos/subImageWidth == pos ) 
   if (vmp.n == pos ) 
    {
//     ClearVMP(vmp);
     NullRectangle(vmp.xpos,vmp.ypos,vmp.width,vmp.height);
/*
     VideoFrameStoreList::FrameStore & cifFs = frameStores.GetFrameStore(CIF_WIDTH, CIF_HEIGHT);
     VideoFrameStoreList::FrameStore & cif16Fs = frameStores.GetFrameStore(CIF16_WIDTH, CIF16_HEIGHT);
     if (vmpNum == 1)
     {
      FillCIF4YUVFrame(frameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT).data.GetPointer(), 0, 0, 0);
      FillCIFYUVFrame(cifFs.data.GetPointer(), 0, 0, 0);
      FillCIF16YUVFrame(cif16Fs.data.GetPointer(), 0, 0, 0);
     }
     else
     {
      FillCIF4YUVRect(frameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT).data.GetPointer(), 0, 0, 0, vmp.xpos, vmp.ypos, vmp.width, vmp.height);
      FillCIFYUVRect(cifFs.data.GetPointer(), 0, 0, 0, vmp.xpos/2, vmp.ypos/2, vmp.width/2, vmp.height/2);
      FillCIF16YUVRect(cif16Fs.data.GetPointer(), 0, 0, 0, vmp.xpos*2, vmp.ypos*2, vmp.width*2, vmp.height*2);
     } 
     frameStores.InvalidateExcept(CIF4_WIDTH, CIF4_HEIGHT); cifFs.valid = 1; cif16Fs.valid = 1;
*/
     if(flag) { VMPListDelVMP(r); delete r; } // static pos
     else { vmp.status = 0; vmp.id = (void *)pos; } // vad pos
     return;
    }
   r = r->next;
  }
}

void MCUSimpleVideoMixer::MyRemoveVideoSourceById(ConferenceMemberId id, BOOL flag)
{
  PWaitAndSignal m(mutex);

  int pos = GetPositionNum(id);
  int type = GetPositionType(id);
  if(pos >= 0) 
  {
   if(flag==FALSE) MyRemoveVideoSource(pos,(type>1)?FALSE:TRUE);
   else MyRemoveVideoSource(pos,TRUE);
  }
}


void MCUSimpleVideoMixer::MyRemoveAllVideoSource()
{
 PWaitAndSignal m(mutex);

 cout << "MyRemoveAllVideoSource\n";

 VMPListClear();
/*
 VideoFrameStoreList::FrameStore & cifFs = frameStores.GetFrameStore(CIF_WIDTH, CIF_HEIGHT);
 VideoFrameStoreList::FrameStore & cif16Fs = frameStores.GetFrameStore(CIF16_WIDTH, CIF16_HEIGHT);
 FillCIF4YUVFrame(frameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT).data.GetPointer(), 0, 0, 0);
 FillCIFYUVFrame(cifFs.data.GetPointer(), 0, 0, 0);
 FillCIF16YUVFrame(cif16Fs.data.GetPointer(), 0, 0, 0);
 frameStores.InvalidateExcept(CIF4_WIDTH, CIF4_HEIGHT); cifFs.valid = 1; cif16Fs.valid = 1;
*/
 NullAllFrameStores();
}

void MCUSimpleVideoMixer::NullAllFrameStores(){
  PWaitAndSignal m(mutex);
  VideoFrameStoreList::VideoFrameStoreListMapType::iterator r; // trying write to all using frames
  for (r=frameStores.videoFrameStoreList.begin(); r!=frameStores.videoFrameStoreList.end(); r++){
    VideoFrameStoreList::FrameStore & vf = *(r->second);
//    if(vf.used<=0) continue;
    if(vf.width<2 || vf.height<2) continue; // minimum size 2*2
    vf.used--; //PINDEX amount=vf.width*vf.height*3/2;
//    if (!OpenMCU::Current().GetPreMediaFrame(vf.data.GetPointer(), vf.width, vf.height, amount))
      FillYUVFrame(vf.data.GetPointer(), 0, 0, 0, vf.width, vf.height);
    vf.valid=1;
  }
}

void MCUSimpleVideoMixer::NullRectangle(int x, int y, int w, int h){
  PWaitAndSignal m(mutex);
  VideoFrameStoreList::VideoFrameStoreListMapType::iterator r; // trying write to all using frames
  for (r=frameStores.videoFrameStoreList.begin(); r!=frameStores.videoFrameStoreList.end(); r++){
    VideoFrameStoreList::FrameStore & vf = *(r->second);
//    if(vf.used<=0) continue;
    if(vf.width<2 || vf.height<2) continue; // minimum size 2*2
    vf.used--;
    int pw=w*vf.width/CIF4_WIDTH; // pixel w&h of vmp-->fs
    int ph=h*vf.height/CIF4_HEIGHT;
    if(pw<2 || ph<2) continue; //PINDEX amount=pw*ph*3/2;
    imageStores_operational_size(pw,ph,_IMGST);
    const void *ist = imageStore.GetPointer();
//    if (!OpenMCU::Current().GetPreMediaFrame(imageStore.GetPointer(), pw, ph, amount))
      FillYUVFrame(imageStore.GetPointer(), 0, 0, 0, pw, ph);
    int px=x*vf.width/CIF4_WIDTH; // pixel x&y of vmp-->fs
    int py=y*vf.height/CIF4_HEIGHT;
    CopyRectIntoFrame(ist,vf.data.GetPointer(),px,py,pw,ph,vf.width,vf.height);
/*    CopyRFromRIntoR(ist,
      ist, vf.GetPointer(),
        px, py, pw, ph,
        0,0,pw,ph,
        vf.width, vf.height,
        pw, ph
    ); */
//    frameStores.InvalidateExcept(vf.width, vf.height);
    vf.valid=1;
  }
}

void MCUSimpleVideoMixer::WriteArbitrarySubFrame(VideoMixPosition & vmp, const void * buffer, int width, int height, PINDEX amount)
{
  VMPCfgOptions & vmpcfg=OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n];
  PWaitAndSignal m(mutex);
  VideoFrameStoreList::VideoFrameStoreListMapType::iterator r; // trying write to all using frames
  for (r=frameStores.videoFrameStoreList.begin(); r!=frameStores.videoFrameStoreList.end(); r++){
    VideoFrameStoreList::FrameStore & vf = *(r->second);
    if(vf.used<=0) continue;
    if(vf.width<2 || vf.height<2) continue; // minimum size 2*2

    vf.used--;

#if USE_FREETYPE
    // printing subtitles in source frame buffer (fast):
    if(!(vmpcfg.label_mask&(FT_P_REALTIME+FT_P_DISABLED))) Print_Subtitles(vmp,(void *)buffer,width,height,vmpcfg.label_mask);
#endif

    int pw=vmp.width*vf.width/CIF4_WIDTH; // pixel w&h of vmp-->fs
    int ph=vmp.height*vf.height/CIF4_HEIGHT;
    if(pw<2 || ph<2) continue;
//PTRACE(6,"WriteArbitrarySubFrame\t" << vmp.n << "(" << width << "x" << height << ") -> [" << vf.width << "x" << vf.height << "]: " << vf.valid << "/" << vf.used);

//    const void *ist = imageStore.GetPointer();
    const void *ist;
    if(pw==width && ph==height) ist = buffer; //same size
    else if(pw*height<ph*width){
      imageStores_operational_size(ph*width/height,ph,_IMGST+_IMGST1);
      ResizeYUV420P((const BYTE *)buffer,    imageStore1.GetPointer(), width, height, ph*width/height, ph);
      CopyRectFromFrame         (imageStore1.GetPointer(),imageStore.GetPointer() , (ph*width/height-pw)/2, 0, pw, ph, ph*width/height, ph);
      ist=imageStore.GetPointer();
    }
    else if(pw*height>ph*width){
      imageStores_operational_size(pw,pw*height/width,_IMGST+_IMGST1);
      ResizeYUV420P((const BYTE *)buffer,    imageStore1.GetPointer(), width, height, pw, pw*height/width);
      CopyRectFromFrame         (imageStore1.GetPointer(),imageStore.GetPointer() , 0, (pw*height/width-ph)/2, pw, ph, pw, pw*height/width);
      ist=imageStore.GetPointer();
    }
    else { // fit. scale
      imageStores_operational_size(pw,ph,_IMGST);
      ResizeYUV420P((const BYTE *)buffer,    imageStore.GetPointer() , width, height, pw, ph);
      ist=imageStore.GetPointer();
    }
    // border (split lines):
    if (vmpcfg.border) VideoSplitLines((void *)ist, vmp, pw, ph);

#if USE_FREETYPE
    // printing subtitles (realtime rendering):
    if((vmpcfg.label_mask&(FT_P_REALTIME+FT_P_DISABLED)) == FT_P_REALTIME) Print_Subtitles(vmp,(void *)ist,pw,ph,vmpcfg.label_mask);
#endif

    int px=vmp.xpos*vf.width/CIF4_WIDTH; // pixel x&y of vmp-->fs
    int py=vmp.ypos*vf.height/CIF4_HEIGHT;
    for(unsigned i=0;i<vmpcfg.blks;i++) CopyRFromRIntoR( ist, vf.data.GetPointer(), px, py, pw, ph,
        vmpcfg.blk[i].posx*vf.width/CIF4_WIDTH, vmpcfg.blk[i].posy*vf.height/CIF4_HEIGHT,
        vmpcfg.blk[i].width*vf.width/CIF4_WIDTH, vmpcfg.blk[i].height*vf.height/CIF4_HEIGHT,
        vf.width, vf.height, pw, ph );

//    frameStores.InvalidateExcept(vf.width, vf.height);
    vf.valid=1;
  }
}

/*
void MCUSimpleVideoMixer::WriteCIF16SubFrame(VideoMixPosition & vmp, const void * buffer, PINDEX amount)
{
  PWaitAndSignal m(mutex);

  VideoFrameStoreList::FrameStore & cif16Fs = frameStores.GetFrameStore(CIF16_WIDTH, CIF16_HEIGHT);
  VideoFrameStoreList::FrameStore & cif4Fs = frameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT);
  VideoFrameStoreList::FrameStore & cifFs = frameStores.GetFrameStore(CIF_WIDTH, CIF_HEIGHT);
  
  if(cif16Fs.used<=0 && cif4Fs.used<=0 && cifFs.used<=0) return;
  if(cif16Fs.used>0) cif16Fs.used--;
  if(cif4Fs.used>0) cif4Fs.used--;
  if(cifFs.used>0) cifFs.used--;

  if(!(OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].label_mask&32)) Print_Subtitles(vmp,(void *)buffer,CIF16_WIDTH,CIF16_HEIGHT,OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].label_mask);

  const void *ist = imageStore.GetPointer();
  if(vmp.width==CIF4_WIDTH && vmp.height==CIF4_HEIGHT) //1x1
  {
   ist = buffer;
  }
  else if(vmp.width==CIF_WIDTH && vmp.height==CIF4_HEIGHT) //2x1
  {
   CopyRectFromFrame((const BYTE *)buffer,imageStore.GetPointer(),CIF_WIDTH,0,CIF4_WIDTH,CIF16_HEIGHT,CIF16_WIDTH,CIF16_HEIGHT);
  }
  else if(vmp.width==CIF_WIDTH && vmp.height==CIF_HEIGHT) //2x2
  {
   ConvertCIF16ToCIF4((const BYTE *)buffer,imageStore.GetPointer());
  }
  else if(vmp.width==Q3CIF4_WIDTH && vmp.height==Q3CIF4_HEIGHT) //1+5
  {
   ConvertCIF16ToQ3CIF16((const BYTE *)buffer,imageStore.GetPointer());
  }
  else if(vmp.width==Q3CIF_WIDTH && vmp.height==Q3CIF_HEIGHT) //3x3
  {
   ConvertCIF16ToQ3CIF4((const BYTE *)buffer,imageStore.GetPointer());
  }
  else if(vmp.width==QCIF_WIDTH && vmp.height==QCIF_HEIGHT) //4x4
  {
   ConvertCIF16ToCIF((const BYTE *)buffer,imageStore.GetPointer());
  }
  else if(vmp.width==TQCIF_WIDTH && vmp.height==TQCIF_HEIGHT) //1+7
  {
   ConvertCIF16ToTCIF((const BYTE *)buffer,imageStore.GetPointer());
  }
  else// if(vmp.width!=CIF4_WIDTH || vmp.height!=CIF4_HEIGHT)
  {
    if(vmp.width*CIF16_HEIGHT<vmp.height*CIF16_WIDTH){
      ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore1.GetPointer(),CIF16_WIDTH,CIF16_HEIGHT,(vmp.height<<1)*11/9,vmp.height<<1);
      CopyRectFromFrame(imageStore1.GetPointer(),imageStore.GetPointer(),((vmp.height<<1)*11/9-(vmp.width<<1))/2,0,vmp.width<<1,vmp.height<<1,(vmp.height<<1)*11/9,vmp.height<<1);
    }
    else if(vmp.width*CIF16_HEIGHT>vmp.height*CIF16_WIDTH){
      ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore1.GetPointer(),CIF16_WIDTH,CIF16_HEIGHT,vmp.width<<1,(vmp.width<<1)*9/11);
      CopyRectFromFrame(imageStore1.GetPointer(),imageStore.GetPointer(),0,((vmp.width<<1)*9/11-(vmp.height<<1))/2,vmp.width<<1,vmp.height<<1,vmp.width<<1,(vmp.width<<1)*9/11);
    }
    else { // fit. scale
      ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore.GetPointer(),CIF16_WIDTH,CIF16_HEIGHT,vmp.width<<1,vmp.height<<1);
    }
  }

  if(cif16Fs.used>0)
  {
    if(OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].border) VideoSplitLines((void *)ist,vmp,vmp.width*2,vmp.height*2);
    for(unsigned i=0;i<OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blks;i++)
    {
       CopyRFromRIntoR(ist,cif16Fs.data.GetPointer(),
        vmp.xpos*2, vmp.ypos*2, vmp.width*2, vmp.height*2,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posx << 1,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posy << 1,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].width << 1,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].height << 1,
        CIF16_WIDTH,
        CIF16_HEIGHT,
        vmp.width*2, vmp.height*2
       );
    }
  }

  if(cif4Fs.used>0 || cifFs.used>0) Convert2To1(ist,imageStore1.GetPointer(),vmp.width*2,vmp.height*2);
  if(cif4Fs.used>0)
  {
    if(OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].border) {
      void *ist4=imageStore1.GetPointer();
      VideoSplitLines((void *)ist4,vmp,vmp.width,vmp.height);
    }
    for(unsigned i=0;i<OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blks;i++)
    {
       CopyRFromRIntoR(imageStore1.GetPointer(),cif4Fs.data.GetPointer(),
        vmp.xpos, vmp.ypos, vmp.width, vmp.height,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posx,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posy,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].width,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].height,
        CIF4_WIDTH,
        CIF4_HEIGHT,
        vmp.width, vmp.height
       );
    }
  }

//  if(cifFs.used>0) Convert2To1(imageStore1.GetPointer(),imageStore.GetPointer(),vmp.width,vmp.height);
  if(cifFs.used>0) {
    if(!(vmp.width&3))Convert2To1(imageStore1.GetPointer(),imageStore.GetPointer(),vmp.width,vmp.height);
    else ConvertFRAMEToCUSTOM_FRAME(imageStore1.GetPointer(),imageStore.GetPointer(),vmp.width,vmp.height,(vmp.width+1)/2,(vmp.height+1)/2);
  }

  if(cifFs.used>0)
  {
    if(OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].border) VideoSplitLines((void *)ist,vmp,(vmp.width+1)/2,(vmp.height+1)/2);
    for(unsigned i=0;i<OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blks;i++)
    {
       CopyRFromRIntoR(imageStore.GetPointer(),cifFs.data.GetPointer(),
        (vmp.xpos+1)/2, (vmp.ypos+1)/2, (vmp.width+1)/2, (vmp.height+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posx+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posy+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].width+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].height+1)/2,
        CIF_WIDTH,
        CIF_HEIGHT,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posx+OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].width+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posy+OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].height+1)/2
       );
    }
  }

  frameStores.InvalidateExcept(CIF16_WIDTH, CIF16_HEIGHT);
  cif4Fs.valid=1; cifFs.valid=1;
  
  return;
}

void MCUSimpleVideoMixer::WriteCIF4SubFrame(VideoMixPosition & vmp, const void * buffer, PINDEX amount)
{
  PWaitAndSignal m(mutex);

  VideoFrameStoreList::FrameStore & cif16Fs = frameStores.GetFrameStore(CIF16_WIDTH, CIF16_HEIGHT);
  VideoFrameStoreList::FrameStore & cif4Fs = frameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT);
  VideoFrameStoreList::FrameStore & cifFs = frameStores.GetFrameStore(CIF_WIDTH, CIF_HEIGHT);
  
  if(cif16Fs.used<=0 && cif4Fs.used<=0 && cifFs.used<=0) return;
  if(cif16Fs.used>0) cif16Fs.used--;
  if(cif4Fs.used>0) cif4Fs.used--;
  if(cifFs.used>0) cifFs.used--;

  const void *ist16 = imageStore1.GetPointer();
  const void *ist4 = imageStore.GetPointer();
  const void *ist = imageStore1.GetPointer();

  if(!(OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].label_mask&32)) Print_Subtitles(vmp,(void *)buffer,CIF4_WIDTH,CIF4_HEIGHT,OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].label_mask);

  if(vmp.width==CIF4_WIDTH && vmp.height==CIF4_HEIGHT) //1x1
  {
   ist16 = imageStore.GetPointer();
   ist4 = buffer;
   if(cif16Fs.used>0) Convert1To2((const BYTE *)buffer,imageStore.GetPointer(),CIF4_WIDTH,CIF4_HEIGHT);
  }
  else if(vmp.width==CIF_WIDTH && vmp.height==CIF4_HEIGHT) //2x1
  {
   if(cif16Fs.used>0) Convert1To2((const BYTE *)buffer,imageStore.GetPointer(),CIF4_WIDTH,CIF4_HEIGHT);
   if(cif16Fs.used>0) CopyRectFromFrame(imageStore.GetPointer(),imageStore1.GetPointer(),CIF_WIDTH,0,CIF4_WIDTH,CIF16_HEIGHT,CIF16_WIDTH,CIF16_HEIGHT);
   if(cif4Fs.used>0) CopyRectFromFrame((const BYTE *)buffer,imageStore.GetPointer(),QCIF_WIDTH,0,CIF_WIDTH,CIF4_HEIGHT,CIF4_WIDTH,CIF4_HEIGHT);
  }
  else if(vmp.width==CIF_WIDTH && vmp.height==CIF_HEIGHT) //2x2
  {
   ist16 = buffer;
   ist4 = imageStore.GetPointer();
   if(cif4Fs.used>0 || cifFs.used>0) Convert2To1((const BYTE *)buffer,imageStore.GetPointer(),CIF4_WIDTH,CIF4_HEIGHT);
  }
  else if(vmp.width==Q3CIF4_WIDTH && vmp.height==Q3CIF4_HEIGHT) //1+5
  {
   if(cif16Fs.used>0) Convert1To2((const BYTE *)buffer,imageStore.GetPointer(),CIF4_WIDTH,CIF4_HEIGHT);
   if(cif16Fs.used>0) ConvertCIF16ToQ3CIF16(imageStore.GetPointer(),imageStore1.GetPointer());
   if(cif4Fs.used>0 || cifFs.used>0) ConvertCIF4ToQ3CIF4((const BYTE *)buffer,imageStore.GetPointer());
  }
  else if(vmp.width==Q3CIF_WIDTH && vmp.height==Q3CIF_HEIGHT) //3x3
  {
   if(cif16Fs.used>0) ConvertCIF4ToQ3CIF4((const BYTE *)buffer,imageStore1.GetPointer());
   if(cif4Fs.used>0 || cifFs.used>0) ConvertCIF4ToQ3CIF((const BYTE *)buffer,imageStore.GetPointer());
  }
  else if(vmp.width==QCIF_WIDTH && vmp.height==QCIF_HEIGHT) //4x4
  {
   if(cif16Fs.used>0) ConvertCIF4ToCIF((const BYTE *)buffer,imageStore1.GetPointer());
   if(cif4Fs.used>0 || cifFs.used>0) ConvertCIF4ToQCIF((const BYTE *)buffer,imageStore.GetPointer());
  }
  else if(vmp.width==TQCIF_WIDTH && vmp.height==TQCIF_HEIGHT) //1+7
  {
   if(cif16Fs.used>0) ConvertCIF4ToTCIF((const BYTE *)buffer,imageStore1.GetPointer());
   if(cif4Fs.used>0 || cifFs.used>0) ConvertCIF4ToTQCIF((const BYTE *)buffer,imageStore.GetPointer());
  }
  else// if(vmp.width!=CIF4_WIDTH || vmp.height!=CIF4_HEIGHT)
  {
    if(vmp.width*CIF4_HEIGHT<vmp.height*CIF4_WIDTH){
      if(cif16Fs.used>0){
        ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore2.GetPointer(),CIF4_WIDTH,CIF4_HEIGHT,(vmp.height<<1)*11/9,vmp.height<<1);
        CopyRectFromFrame(imageStore2.GetPointer(),imageStore1.GetPointer(),((vmp.height<<1)*11/9-(vmp.width<<1))/2,0,vmp.width<<1,vmp.height<<1,(vmp.height<<1)*11/9,vmp.height<<1);
      }
      if(cif4Fs.used || cifFs.used){
        ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore2.GetPointer(),CIF4_WIDTH,CIF4_HEIGHT,vmp.height*11/9,vmp.height);
        CopyRectFromFrame(imageStore2.GetPointer(),imageStore.GetPointer(),(vmp.height*11/9-vmp.width)/2,0,vmp.width,vmp.height,vmp.height*11/9,vmp.height);
      }
    }
    else if(vmp.width*CIF4_HEIGHT>vmp.height*CIF4_WIDTH){
      if(cif16Fs.used>0){
        ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore2.GetPointer(),CIF4_WIDTH,CIF4_HEIGHT,vmp.width<<1,(vmp.width<<1)*9/11);
        CopyRectFromFrame(imageStore2.GetPointer(),imageStore1.GetPointer(),0,((vmp.width<<1)*9/11-(vmp.height<<1))/2,vmp.width<<1,vmp.height<<1,vmp.width<<1,(vmp.width<<1)*9/11);
      }
      if(cif4Fs.used || cifFs.used){
        ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore2.GetPointer(),CIF4_WIDTH,CIF4_HEIGHT,vmp.width,vmp.width*9/11);
        CopyRectFromFrame(imageStore2.GetPointer(),imageStore.GetPointer(),0,(vmp.width*9/11-vmp.height)/2,vmp.width,vmp.height,vmp.width,vmp.width*9/11);
      }
    }
    else { // fit. scale
      if(cif16Fs.used>0) ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore1.GetPointer(),CIF4_WIDTH,CIF4_HEIGHT,vmp.width<<1,vmp.height<<1);
      if(cif4Fs.used || cifFs.used) ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore.GetPointer(),CIF4_WIDTH,CIF4_HEIGHT,vmp.width,vmp.height);
    }
  }

  if(cif16Fs.used>0)
  {
    if(OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].border) VideoSplitLines((void *)ist16,vmp,vmp.width*2,vmp.height*2);
    for(unsigned i=0;i<OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blks;i++)
    {
       CopyRFromRIntoR(ist16,cif16Fs.data.GetPointer(),
        vmp.xpos*2, vmp.ypos*2, vmp.width*2, vmp.height*2,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posx << 1,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posy << 1,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].width << 1,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].height << 1,
        CIF16_WIDTH,
        CIF16_HEIGHT,
        vmp.width*2,vmp.height*2
       );
    }
  }

  if(cif4Fs.used>0)
  {
    if(OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].border) VideoSplitLines((void *)ist4,vmp,vmp.width,vmp.height);
    for(unsigned i=0;i<OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blks;i++)
    {
       CopyRFromRIntoR(ist4,cif4Fs.data.GetPointer(),
        vmp.xpos, vmp.ypos, vmp.width, vmp.height,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posx,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posy,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].width,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].height,
        CIF4_WIDTH,
        CIF4_HEIGHT,
        vmp.width*2,vmp.height*2
       );
    }
  }

  if(cifFs.used>0) {
    if(!(vmp.width&3))Convert2To1(ist4,imageStore1.GetPointer(),vmp.width,vmp.height);
    else ConvertFRAMEToCUSTOM_FRAME(ist4,imageStore1.GetPointer(),vmp.width,vmp.height,(vmp.width+1)/2,(vmp.height+1)/2);
  }

  if(cifFs.used>0)
  {
    if(OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].border) VideoSplitLines((void *)ist,vmp,(vmp.width+1)/2,(vmp.height+1)/2);
    for(unsigned i=0;i<OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blks;i++)
    {
       CopyRFromRIntoR(ist,cifFs.data.GetPointer(),
        (vmp.xpos+1)/2, (vmp.ypos+1)/2, (vmp.width+1)/2, (vmp.height+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posx+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posy+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].width+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].height+1)/2,
        CIF_WIDTH,
        CIF_HEIGHT,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posx+OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].width+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posy+OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].height+1)/2
       );
    }
  }

  frameStores.InvalidateExcept(CIF16_WIDTH, CIF16_HEIGHT);
  cif4Fs.valid=1; cifFs.valid=1;
  
  return;
}

void MCUSimpleVideoMixer::WriteCIFSubFrame(VideoMixPosition & vmp, const void * buffer, PINDEX amount)
{
  PWaitAndSignal m(mutex);

  VideoFrameStoreList::FrameStore & cif16Fs = frameStores.GetFrameStore(CIF16_WIDTH, CIF16_HEIGHT);
  VideoFrameStoreList::FrameStore & cif4Fs = frameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT);
  VideoFrameStoreList::FrameStore & cifFs = frameStores.GetFrameStore(CIF_WIDTH, CIF_HEIGHT);
  
  if(cif16Fs.used<=0 && cif4Fs.used<=0 && cifFs.used<=0) return;
  if(cif16Fs.used>0) cif16Fs.used--;
  if(cif4Fs.used>0) cif4Fs.used--;
  if(cifFs.used>0) cifFs.used--;

  const void *ist16 = imageStore1.GetPointer();
  const void *ist4 = imageStore.GetPointer();
  const void *ist = imageStore1.GetPointer();

  if(!(OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].label_mask&32)) Print_Subtitles(vmp,(void *)buffer,CIF_WIDTH,CIF_HEIGHT,OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].label_mask);

  if(vmp.width==CIF4_WIDTH && vmp.height==CIF4_HEIGHT) //1x1
  {
   ist = buffer;
   if(cif16Fs.used>0 || cif4Fs.used>0) Convert1To2((const BYTE *)buffer,imageStore.GetPointer(),CIF_WIDTH,CIF_HEIGHT);
  }
  else if(vmp.width==CIF_WIDTH && vmp.height==CIF4_HEIGHT) //2x1
  {
   if(cif16Fs.used>0 || cif4Fs.used>0) CopyRectFromFrame((const BYTE *)buffer,imageStore1.GetPointer(),SQCIF_WIDTH,0,QCIF_WIDTH,CIF_HEIGHT,CIF_WIDTH,CIF_HEIGHT);
   if(cif16Fs.used>0 || cif4Fs.used>0) Convert1To2(imageStore1.GetPointer(),imageStore.GetPointer(),QCIF_WIDTH,CIF_HEIGHT);
   CopyRectFromFrame((const BYTE *)buffer,imageStore1.GetPointer(),SQCIF_WIDTH,0,QCIF_WIDTH,CIF_HEIGHT,CIF_WIDTH,CIF_HEIGHT);
  }
  else if(vmp.width==CIF_WIDTH && vmp.height==CIF_HEIGHT) //2x2
  {
   ist4 = buffer;
   ist = imageStore.GetPointer();
   if(cifFs.used>0) Convert2To1((const BYTE *)buffer,imageStore.GetPointer(),CIF_WIDTH,CIF_HEIGHT);
  }
  else if(vmp.width==Q3CIF4_WIDTH && vmp.height==Q3CIF4_HEIGHT) //1+5
  {
   if(cif4Fs.used>0 || cif16Fs.used>0) Convert1To2((const BYTE *)buffer,imageStore1.GetPointer(),CIF_WIDTH,CIF_HEIGHT);
   if(cif4Fs.used>0 || cif16Fs.used>0) ConvertCIF4ToQ3CIF4(imageStore1.GetPointer(),imageStore.GetPointer());
   if(cifFs.used>0) ConvertCIFToQ3CIF((const BYTE *)buffer,imageStore1.GetPointer());
  }
  else if(vmp.width==Q3CIF_WIDTH && vmp.height==Q3CIF_HEIGHT) //3x3
  {
   if(cif4Fs.used>0 || cif16Fs.used>0) ConvertCIFToQ3CIF((const BYTE *)buffer,imageStore.GetPointer());
   if(cifFs.used>0) ConvertCIFToSQ3CIF((const BYTE *)buffer,imageStore1.GetPointer());
  }
  else if(vmp.width==QCIF_WIDTH && vmp.height==QCIF_HEIGHT) //4x4
  {
   ist16 = buffer;
   if(cif4Fs.used>0 || cif16Fs.used>0) ConvertCIFToQCIF((const BYTE *)buffer,imageStore.GetPointer());
   if(cifFs.used>0) ConvertCIFToSQCIF((const BYTE *)buffer,imageStore1.GetPointer());
  }
  else if(vmp.width==SQ5CIF_WIDTH && vmp.height==SQ5CIF_HEIGHT) //5x5
  {
   if(cif4Fs.used>0 || cif16Fs.used>0) ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore.GetPointer(),CIF_WIDTH,CIF_HEIGHT,SQ5CIF_WIDTH,SQ5CIF_HEIGHT);
   if(cifFs.used>0) ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore1.GetPointer(),CIF_WIDTH,CIF_HEIGHT,SQ5CIF_WIDTH/2,SQ5CIF_HEIGHT/2);
  }
  else if(vmp.width==SQ3CIF_WIDTH && vmp.height==SQ3CIF_HEIGHT) //6x6
  {
   if(cif4Fs.used>0 || cif16Fs.used>0) ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore.GetPointer(),CIF_WIDTH,CIF_HEIGHT,SQ3CIF_WIDTH,SQ3CIF_HEIGHT);
   if(cifFs.used>0) ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore1.GetPointer(),CIF_WIDTH,CIF_HEIGHT,SQ3CIF_WIDTH/2,SQ3CIF_HEIGHT/2);
  }
  else if(vmp.width==TQCIF_WIDTH && vmp.height==TQCIF_HEIGHT) //1+7
  {
   if(cif4Fs.used>0 || cif16Fs.used>0) ConvertCIFToTQCIF((const BYTE *)buffer,imageStore.GetPointer());
   if(cifFs.used>0) ConvertCIFToTSQCIF((const BYTE *)buffer,imageStore1.GetPointer());
  }
  else// if(vmp.width!=CIF_WIDTH || vmp.height!=CIF_HEIGHT)
  {
    if(vmp.width*CIF_HEIGHT<vmp.height*CIF_WIDTH){
      if(cif16Fs.used || cif4Fs.used){
        ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore2.GetPointer(),CIF_WIDTH,CIF_HEIGHT,vmp.height*11/9,vmp.height);
        CopyRectFromFrame(imageStore2.GetPointer(),imageStore.GetPointer(),(vmp.height*11/9-vmp.width)/2,0,vmp.width,vmp.height,vmp.height*11/9,vmp.height);
      }
      if(cifFs.used){
        ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore2.GetPointer(),CIF_WIDTH,CIF_HEIGHT,(vmp.height*11/9+1)/2,(vmp.height+1)/2);
        CopyRectFromFrame(imageStore2.GetPointer(),imageStore1.GetPointer(),(vmp.height*11/9/2-vmp.width/2+1)/2,0,(vmp.width+1)/2,(vmp.height+1)/2,(vmp.height*11/9+1)/2,(vmp.height+1)/2);
      }
    }
    else if(vmp.width*CIF_HEIGHT>vmp.height*CIF_WIDTH){
      if(cif16Fs.used || cif4Fs.used){
        ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore2.GetPointer(),CIF_WIDTH,CIF_HEIGHT,vmp.width,vmp.width*9/11);
        CopyRectFromFrame(imageStore2.GetPointer(),imageStore.GetPointer(),0,(vmp.width*9/11-vmp.height)/2,vmp.width,vmp.height,vmp.width,vmp.width*9/11);
      }
      if(cifFs.used){
        ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore2.GetPointer(),CIF_WIDTH,CIF_HEIGHT,vmp.width/2,vmp.width*9/11/2);
        CopyRectFromFrame(imageStore2.GetPointer(),imageStore1.GetPointer(),0,(vmp.width*9/11/2-vmp.height/2)/2,vmp.width/2,vmp.height/2,vmp.width/2,vmp.width*9/11/2);
      }
    }
    else { // fit. scale
      if(cif16Fs.used || cif4Fs.used) ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore.GetPointer(),CIF_WIDTH,CIF_HEIGHT,vmp.width,vmp.height);
      if(cifFs.used) ConvertFRAMEToCUSTOM_FRAME((const BYTE *)buffer,imageStore1.GetPointer(),CIF_WIDTH,CIF_HEIGHT,vmp.width/2,vmp.height/2);
    }
  }

  if(cifFs.used>0)
  {
    if(OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].border) VideoSplitLines((void *)ist,vmp,(vmp.width+1)/2,(vmp.height+1)/2);
    for(unsigned i=0;i<OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blks;i++)
    {
       CopyRFromRIntoR(ist,cifFs.data.GetPointer(),
        (vmp.xpos+1)/2, (vmp.ypos+1)/2, (vmp.width+1)/2, (vmp.height+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posx+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posy+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].width+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].height+1)/2,
        CIF_WIDTH,
        CIF_HEIGHT,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posx+OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].width+1)/2,
        (OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posy+OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].height+1)/2
       );
    }
  }

  if(cif4Fs.used>0)
  {
    if(OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].border) VideoSplitLines(imageStore.GetPointer(),vmp,vmp.width,vmp.height);
    for(unsigned i=0;i<OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blks;i++)
    {
       CopyRFromRIntoR(ist4,cif4Fs.data.GetPointer(),
        vmp.xpos, vmp.ypos, vmp.width, vmp.height,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posx,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posy,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].width,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].height,
        CIF4_WIDTH,
        CIF4_HEIGHT,
        vmp.width,vmp.height
       );
    }
  }

  if(cif16Fs.used>0 && (vmp.width!=QCIF_WIDTH || vmp.height!=QCIF_HEIGHT)) 
    Convert1To2(ist4,imageStore1.GetPointer(),vmp.width,vmp.height);

  if(cif16Fs.used>0)
  {
    if(OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].border) VideoSplitLines(imageStore1.GetPointer(),vmp,vmp.width*2,vmp.height*2);
    for(unsigned i=0;i<OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blks;i++)
    {
       CopyRFromRIntoR(ist16,cif16Fs.data.GetPointer(),
        vmp.xpos << 1, vmp.ypos << 1, vmp.width << 1, vmp.height << 1,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posx << 1,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].posy << 1,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].width << 1,
        OpenMCU::vmcfg.vmconf[specialLayout].vmpcfg[vmp.n].blk[i].height << 1,
        CIF16_WIDTH,
        CIF16_HEIGHT,
        vmp.width*2,vmp.height*2
       );
    }
  }

  frameStores.InvalidateExcept(CIF16_WIDTH, CIF16_HEIGHT);
  cif4Fs.valid=1; cifFs.valid=1;
  
  return;
}
*/

BOOL MCUSimpleVideoMixer::WriteSubFrame(VideoMixPosition & vmp, const void * buffer, int width, int height, PINDEX amount)
{
WriteArbitrarySubFrame(vmp,buffer,width,height,amount); return TRUE; /*
 if(width==CIF_WIDTH && height==CIF_HEIGHT) 
  { WriteCIFSubFrame(vmp,buffer,amount); return TRUE; }
 if(width==CIF4_WIDTH && height==CIF4_HEIGHT)
  { WriteCIF4SubFrame(vmp,buffer,amount); return TRUE; }
 if(width==CIF16_WIDTH && height==CIF16_HEIGHT)
  { WriteCIF16SubFrame(vmp,buffer,amount); return TRUE; }

 int nw,nh;
  
 if(width*CIF_HEIGHT == height*CIF_WIDTH) { nw=width; nh=height; }
 else if(width*CIF_HEIGHT > height*CIF_WIDTH) // needs h cut
  { nw=(height*CIF_WIDTH)/CIF_HEIGHT; nh=height; }
 else { nw=width; nh=(width*CIF_HEIGHT)/CIF_WIDTH; }

 PWaitAndSignal m(mutex);

//added by kay27, testing needed:
 if(width*vmp.height<height*vmp.width)
 {
  nw=width; nh=(width*CIF_HEIGHT)/CIF_WIDTH;
  FillYUVRect(imageStore.GetPointer(),nw,nh,0x80,0x80,0x80,0,0,nw,(nh-height)>>1);
  CopyRectIntoFrame((const BYTE *)buffer,imageStore.GetPointer(),0,(nh-height)>>1,width,height,nw,nh);
  FillYUVRect(imageStore.GetPointer(),nw,nh,0x80,0x80,0x80,0,nh-((nh-height)>>1),nw,(nh-height)>>1);
 } else
//end of addition by kay27//
 CopyRectFromFrame((const BYTE *)buffer,imageStore.GetPointer(),(width-nw)>>1,(height-nh)>>1,nw,nh,width,height);

 if(nw <= CIF_WIDTH*1.5)
 {
  ConvertFRAMEToCUSTOM_FRAME(imageStore.GetPointer(), imageStore2.GetPointer(), nw, nh, CIF_WIDTH, CIF_HEIGHT);
  WriteCIFSubFrame(vmp,imageStore2.GetPointer(),CIF_SIZE); return TRUE;
 }
 else if(nw <= CIF4_WIDTH+100)
 {
  ConvertFRAMEToCUSTOM_FRAME(imageStore.GetPointer(), imageStore2.GetPointer(), nw, nh, CIF4_WIDTH, CIF4_HEIGHT);
  WriteCIF4SubFrame(vmp,imageStore2.GetPointer(),CIF4_SIZE); return TRUE;
 }
 else
 {
  ConvertFRAMEToCUSTOM_FRAME(imageStore.GetPointer(), imageStore2.GetPointer(), nw, nh, CIF16_WIDTH, CIF16_HEIGHT);
  WriteCIF16SubFrame(vmp,imageStore2.GetPointer(),CIF16_SIZE); return TRUE;
 } 

  return TRUE; */
}

///////////////////////////////////////////////////////////////////////////////////////

#if ENABLE_TEST_ROOMS

TestVideoMixer::TestVideoMixer(unsigned _frames)
  : frames(_frames), allocated(FALSE)
{
}

BOOL TestVideoMixer::AddVideoSource(ConferenceMemberId id, ConferenceMember & mbr)
{
  PWaitAndSignal m(mutex);

  if (allocated)
    return FALSE;

  allocated = TRUE;

  unsigned i;
  for (i = 0; i < frames; ++i) {

    // calculate the kind of video split we need to include the new source
    CalcVideoSplitSize(i+1, subImageWidth, subImageHeight, cols, rows);

    VideoMixPosition * newPosition = CreateVideoMixPosition(id, 0, 0, subImageWidth, subImageHeight);
    newPosition->label_init=FALSE;
    VMPListInsVMP(newPosition);

    ReallocatePositions();
  }

  return TRUE;
}

BOOL TestVideoMixer::ReadFrame(ConferenceMember &, void * buffer, int width, int height, PINDEX & amount)
{
  PWaitAndSignal m(mutex);

  // not allocated means fill with black
  if (!allocated) {
    VideoFrameStoreList::FrameStore & fs = frameStores.GetFrameStore(width, height);
    if (!fs.valid) {
      if (!OpenMCU::Current().GetPreMediaFrame(fs.data.GetPointer(), width, height, amount))
        FillYUVFrame(fs.data.GetPointer(), 0, 0, 0, width, height);
      fs.valid = TRUE;
    }
    memcpy(buffer, fs.data.GetPointer(), amount);
    return TRUE;
  }

  return ReadMixedFrame(buffer, width, height, amount);
}


BOOL TestVideoMixer::WriteFrame(ConferenceMemberId id, const void * buffer, int width, int height, PINDEX amount)
{
  return TRUE;
}

#endif // ENABLE_TEST_ROOMS


#if ENABLE_ECHO_MIXER

EchoVideoMixer::EchoVideoMixer()
  : MCUSimpleVideoMixer()
{
}

BOOL EchoVideoMixer::AddVideoSource(ConferenceMemberId id, ConferenceMember & mbr)
{
  PWaitAndSignal m(mutex);

  CalcVideoSplitSize(1, subImageWidth, subImageHeight, cols, rows);
  VideoMixPosition * newPosition = CreateVideoMixPosition(id, 0, 0, subImageWidth, subImageHeight);
  newPosition->label_init=FALSE;
  VMPListInsVMP(newPosition);
  ReallocatePositions();

  return TRUE;
}

BOOL EchoVideoMixer::WriteFrame(ConferenceMemberId id, const void * buffer, int width, int height, PINDEX amount)
{
  PWaitAndSignal m(mutex);

  // write data into sub frame of mixed image
  VideoMixPosition *r = vmpList->next;
  while(r != NULL)
  {
   WriteSubFrame(*r, buffer, width, height, amount);
   r = r->next;
  }

  return TRUE;
}

BOOL EchoVideoMixer::ReadFrame(ConferenceMember &, void * buffer, int width, int height, PINDEX & amount)
{
  return ReadMixedFrame(buffer, width, height, amount);
}


#endif // ENABLE_ECHO_MIXER


VideoMixConfigurator::VideoMixConfigurator(long _w, long _h){
 bfw=_w; bfh=_h;
 go(_w,_h); // just go
}

VideoMixConfigurator::~VideoMixConfigurator(){
 for(unsigned ii=0;ii<vmconfs;ii++) { //attempt to delete all
  vmconf[ii].vmpcfg=(VMPCfgOptions *)realloc((void *)(vmconf[ii].vmpcfg),0);
  vmconf[ii].vmpcfg=NULL;
 }
 vmconfs=0;
 vmconf=(VMPCfgLayout *)realloc((void *)vmconf,0);
 vmconf=NULL;
}

void VideoMixConfigurator::go(unsigned frame_width, unsigned frame_height)
{
  bfw=frame_width; bfh=frame_height;
  FILE *fs; long f_size; char * f_buff;
  fs = fopen(VMPC_CONFIGURATION_NAME,"r"); if(!fs) { cout << "Video Mixer Configurator: ERROR! Can't read file \"" << VMPC_CONFIGURATION_NAME << "\"\n"; return; }
  fseek(fs,0L,SEEK_END); f_size=ftell(fs); rewind(fs);
  f_buff=new char[f_size+1]; fread(f_buff,1,f_size,fs); f_buff[f_size]=0;
  fclose(fs);
  strcpy(fontfile,VMPC_DEFAULT_FONTFILE);
  for(long i=0;i<2;i++){
   fw[i]=bfw; if(fw[i]==0) fw[i]=2;
   fh[i]=bfh; if(fh[i]==0) fh[i]=2;
   strcpy(sopts[i].Id,VMPC_DEFAULT_ID);
   sopts[i].vidnum=VMPC_DEFAULT_VIDNUM;
   sopts[i].mode_mask=VMPC_DEFAULT_MODE_MASK;
   sopts[i].new_from_begin=VMPC_DEFAULT_NEW_FROM_BEGIN;
   sopts[i].reallocate_on_disconnect=VMPC_DEFAULT_REALLOCATE_ON_DISCONNECT;
   sopts[i].mockup_width=VMPC_DEFAULT_MOCKUP_WIDTH;
   sopts[i].mockup_height=VMPC_DEFAULT_MOCKUP_HEIGHT;
   strcpy(sopts[i].tags,VMPC_DEFAULT_TAGS);
   opts[i].posx=VMPC_DEFAULT_POSX;
   opts[i].posy=VMPC_DEFAULT_POSY;
   opts[i].width=VMPC_DEFAULT_WIDTH;
   opts[i].height=VMPC_DEFAULT_HEIGHT;
   opts[i].border=VMPC_DEFAULT_BORDER;
#if USE_FREETYPE
   opts[i].label_mask=VMPC_DEFAULT_LABEL_MASK;
   strcpy(opts[i].label_text,VMPC_DEFAULT_LABEL_TEXT);
   opts[i].label_color=VMPC_DEFAULT_LABEL_COLOR;
   opts[i].label_bgcolor=VMPC_DEFAULT_LABEL_BGCOLOR;
   strcpy(opts[i].fontsize,VMPC_DEFAULT_FONTSIZE);
   strcpy(opts[i].border_left,VMPC_DEFAULT_BORDER_LEFT);
   strcpy(opts[i].border_right,VMPC_DEFAULT_BORDER_RIGHT);
   strcpy(opts[i].border_top,VMPC_DEFAULT_BORDER_TOP);
   strcpy(opts[i].border_bottom,VMPC_DEFAULT_BORDER_BOTTOM);
   opts[i].cut_before_bracket=VMPC_DEFAULT_CUT_BEFORE_BRACKET;
#endif
  }
  parser(f_buff,f_size);
  delete f_buff;
  cout << "VideoMixConfigurator: " << VMPC_CONFIGURATION_NAME << " processed: " << vmconfs << " layout(s) loaded.\n";
/*
  for(unsigned ii=0;ii<vmconfs;ii++){
   cout << "Layout " << vmconf[ii].splitcfg.Id << "\n";
   for (unsigned j=0;j<vmconf[ii].splitcfg.vidnum;j++){
    cout << " *vmp" << j << " ("
     << vmconf[ii].vmpcfg[j].posx << ","
     << vmconf[ii].vmpcfg[j].posy << ","
     << vmconf[ii].vmpcfg[j].width << ","
     << vmconf[ii].vmpcfg[j].height << ")"
     << ": " << vmconf[ii].vmpcfg[j].blks << " blocks:";
    for (unsigned k=0;k<vmconf[ii].vmpcfg[j].blks;k++){
     cout << " -blk" << k << " ("
      << vmconf[ii].vmpcfg[j].blk[k].posx << ","
      << vmconf[ii].vmpcfg[j].blk[k].posy << ","
      << vmconf[ii].vmpcfg[j].blk[k].width << ","
      << vmconf[ii].vmpcfg[j].blk[k].height << ");";
    }
    cout << "\n";
   }
  }
*/
}

void VideoMixConfigurator::parser(char* &f_buff,long f_size)
{
  lid=0; ldm=0;
  long pos1,pos=0;
  long line=0;
  bool escape=false;
  while(pos<f_size){
   pos1=pos;
   while(((f_buff[pos1]!='\n')||escape)&&(pos1<f_size)){
    if(escape&&(f_buff[pos1]=='\n')) line++;
    escape=((f_buff[pos1]=='\\')&&(!escape));
    pos1++;
   }
   line++;
   if(pos!=pos1)handle_line(f_buff,pos,pos1,line);
   pos=pos1+1;
  }
  if(ldm==1)finalize_layout_desc();
  geometry();
}

void VideoMixConfigurator::block_insert(VMPBlock * & b,long b_n,unsigned x,unsigned y,unsigned w,unsigned h){
 if(b_n==0) b=(VMPBlock *)malloc(sizeof(VMPBlock));
 else b=(VMPBlock *)realloc((void *)b,sizeof(VMPBlock)*(b_n+1));
/*
 b[b_n].posx=(x+1)&0xFFFFFE;
 b[b_n].posy=(y+1)&0XFFFFFE;
 b[b_n].width=(w+1)&0xFFFFFE;
 b[b_n].height=(h+1)&0xFFFFFE;
*/
 b[b_n].posx=x;
 b[b_n].posy=y;
 b[b_n].width=w;
 b[b_n].height=h;
// cout << "b.ins[" << x << "," << y << "," << w << "," << h << "]\n";
}

void VideoMixConfigurator::block_erase(VMPBlock * & b,long b_i,long b_n){
// cout << "e.bl[" << b_i << "," << b_n << "]\n";
 if(b_n<1) return;
 for(long i=b_i;i<b_n-1;i++)b[i]=b[i+1];
 if(b_n==1) {
  free((void *)b);
  b=NULL;
 }
 else b=(VMPBlock *)realloc((void *)b,sizeof(VMPBlock)*(b_n-1));
}

unsigned VideoMixConfigurator::frame_splitter(VMPBlock * & b,long b_i,long b_n,unsigned x0,unsigned y0,unsigned w0,unsigned h0,unsigned x1,unsigned y1,unsigned w1,unsigned h1){
// cout << "f.spl[" << b_i << "," << b_n << "," << x0 << "," << y0 << "," << w0 << "," << h0 << "," << x1 << "," << y1 << "," << w1 << "," << h1 << "]\n";
 unsigned x00=x0+w0; unsigned y00=y0+h0; unsigned x11=x1+w1; unsigned y11=y1+h1;
 geometry_changed=true;
 if((x1<=x0)&&(y1<=y0)&&(x11>=x00)&&(y11>=y00)){ // no visible blocks
//cout << "[16]";
  block_erase(b,b_i,b_n);
  return (b_n-1);
 }
 if((x1>x0)&&(y1>y0)&&(x11<x00)&&(y11<y00)){ // somewhere inside completely
//cout << "[1]";
  block_erase(b,b_i,b_n);                                     //////
  block_insert(b,b_n-1,x0   ,y0   ,w0         ,y1-y0      );  //  //
  block_insert(b,b_n  ,x0   ,y1   ,x1-x0      ,h1         );  //////
  block_insert(b,b_n+1,x11  ,y1   ,x00-x11    ,h1         );
  block_insert(b,b_n+2,x0   ,y11  ,w0         ,y00-y11    );
  return b_n+3;
 }

 if((x1>x0)&&(x11<x00)&&(y1<=y0)&&(y11>y0)&&(y11<y00)){ // top middle intersection
//cout << "[2]";
  block_erase(b,b_i,b_n);                                     //  //
  block_insert(b,b_n-1,x0   ,y0   ,x1-x0      ,y11-y0     );  //////
  block_insert(b,b_n  ,x1+w1,y0   ,x00-x11    ,y11-y0     );  //////
  block_insert(b,b_n+1,x0   ,y11  ,w0         ,y00-y11    );
  return b_n+2;
 }
 if((x1>x0)&&(x11<x00)&&(y1>y0)&&(y1<y00)&&(y11>=y00)){ // bottom middle intersection
//cout << "[3]";
  block_erase(b,b_i,b_n);                                     //////
  block_insert(b,b_n-1,x0   ,y0   ,w0         ,y1-y0      );  //////
  block_insert(b,b_n  ,x0   ,y1   ,x1-x0      ,y00-y1     );  //  //
  block_insert(b,b_n+1,x1+w1,y1   ,x00-x11    ,y00-y1     );
  return b_n+2;
 }
 if((x1<=x0)&&(x11>x0)&&(x11<x00)&&(y1>y0)&&(y11<y00)){ // middle left intersection
//cout << "[4]";
  block_erase(b,b_i,b_n);                                     //////
  block_insert(b,b_n-1,x0   ,y0   ,w0         ,y1-y0      );    ////
  block_insert(b,b_n  ,x11  ,y1   ,x00-x11    ,h1         );  //////
  block_insert(b,b_n+1,x0   ,y11  ,w0         ,y00-y11    );
  return b_n+2;
 }
 if((x11>=x00)&&(x1<x00)&&(x1>x0)&&(y1>y0)&&(y11<y00)){ // middle right intersection
//cout << "[5]";
  block_erase(b,b_i,b_n);                                     //////
  block_insert(b,b_n-1,x0   ,y0   ,w0         ,y1-y0      );  ////
  block_insert(b,b_n  ,x0   ,y1   ,x1-x0      ,h1         );  //////
  block_insert(b,b_n+1,x0   ,y11  ,w0         ,y00-y11    );
  return b_n+2;
 }
 if((x1<=x0)&&(x11>x0)&&(x11<x00)&&(y1<=y0)&&(y11>y0)&&(y11<y00)){ // top left intersection
//cout << "[6]";
  block_erase(b,b_i,b_n);                                       ////
  block_insert(b,b_n-1,x11  ,y0   ,x00-x11    ,y11-y0     );  //////
  block_insert(b,b_n  ,x0   ,y11  ,w0         ,y00-y11    );  //////
  return b_n+1;
 }
 if((x11>=x00)&&(x1<x00)&&(x1>x0)&&(y1<=y0)&&(y11>y0)&&(y11<y00)){ // top right intersection
//cout << "[7]";
  block_erase(b,b_i,b_n);                                     ////
  block_insert(b,b_n-1,x0   ,y0   ,x1-x0      ,y11-y0     );  //////
  block_insert(b,b_n  ,x0   ,y11  ,w0         ,y00-y11    );  //////
  return b_n+1;
 }
 if((x1<=x0)&&(x11>x0)&&(x11<x00)&&(y11>=y00)&&(y1<y00)&&(y1>y0)){ // bottom left intersection
//cout << "[8]";
  block_erase(b,b_i,b_n);                                     //////
  block_insert(b,b_n-1,x0   ,y0   ,w0         ,y1-y0      );  //////
  block_insert(b,b_n  ,x11  ,y1   ,x00-x11    ,y00-y1     );    ////
  return b_n+1;
 }
 if((x11>=x00)&&(x1<x00)&&(x1>x0)&&(y11>=y00)&&(y1<y00)&&(y1>y0)){ // bottom right intersection
//cout << "[9]";
  block_erase(b,b_i,b_n);                                     //////
  block_insert(b,b_n-1,x0   ,y0   ,w0         ,y1-y0      );  //////
  block_insert(b,b_n  ,x0   ,y1   ,x1-x0      ,y00-y1     );  ////
  return b_n+1;
 }
 if((x1<=x0)&&(x11>=x00)&&(y1<=y0)&&(y11>y0)&&(y11<y00)){ // all-over top intersection
//cout << "[10]";
  block_erase(b,b_i,b_n);
  block_insert(b,b_n-1,x0   ,y11  ,w0         ,y00-y11    );  //////
  return b_n;                                                 //////
 }
 if((x1<=x0)&&(x11>=x00)&&(y11>=y00)&&(y1<y00)&&(y1>y0)){ // all-over bottom intersection
//cout << "[11]";
  block_erase(b,b_i,b_n);                                     //////
  block_insert(b,b_n-1,x0   ,y0   ,w0         ,y1-y0      );  //////
  return b_n;
 }
 if((x1<=x0)&&(x11>x0)&&(x11<x00)&&(y1<=y0)&&(y11>=y00)){ // all-over left intersection
//cout << "[12]";
  block_erase(b,b_i,b_n);                                       ////
  block_insert(b,b_n-1,x11  ,y0   ,x00-x11    ,h0         );    ////
  return b_n;                                                   ////
 }
 if((x11>=x00)&&(x1<x00)&&(x1>x0)&&(y1<=y0)&&(y11>=y00)){ // all-over left intersection
//cout << "[13]";
  block_erase(b,b_i,b_n);                                     ////
  block_insert(b,b_n-1,x0   ,y0   ,x1-x0      ,h0         );  ////
  return b_n;                                                 ////
 }
 if((x1<=x0)&&(x11>=x00)&&(y1>y0)&&(y11<y00)){ // all-over left-right intersection
//cout << "[14]";
  block_erase(b,b_i,b_n);                                     //////
  block_insert(b,b_n-1,x0   ,y0   ,w0         ,y1-y0      );        
  block_insert(b,b_n  ,x0   ,y11  ,w0         ,y00-y11    );  //////
  return b_n+1;
 }
 if((y1<=y0)&&(y11>=y00)&&(x1>x0)&&(x11<x00)){ // all-over top-bottom intersection
//cout << "[15]";
  block_erase(b,b_i,b_n);                                     //  //
  block_insert(b,b_n-1,x0   ,y0   ,x1-x0      ,h0         );  //  //
  block_insert(b,b_n  ,x11  ,y0   ,x00-x11    ,h0         );  //  //
  return b_n+1;
 }
// cout << "[0]";
 geometry_changed=false;
 return b_n;
}

void VideoMixConfigurator::geometry(){ // find and store visible blocks of frames
 for(unsigned i=0;i<vmconfs;i++)
 {
   for(unsigned j=0;j<vmconf[i].splitcfg.vidnum;j++)
   { // Create single block 0 for each position first:
     block_insert(vmconf[i].vmpcfg[j].blk,0,vmconf[i].vmpcfg[j].posx,vmconf[i].vmpcfg[j].posy,vmconf[i].vmpcfg[j].width,vmconf[i].vmpcfg[j].height);
     vmconf[i].vmpcfg[j].blks=1;
//     cout << "*ctrl/i: i=" << i << " j=" << j << " posx=" << vmconf[i].vmpcfg[j].blk[0].posx << "\n";
   }
   for(unsigned j=0;j<vmconf[i].splitcfg.vidnum-1;j++) for (unsigned k=j+1; k<vmconf[i].splitcfg.vidnum;k++)
   {
     unsigned bn=vmconf[i].vmpcfg[j].blks; //remember initial value of blocks
     unsigned b0=0; // block index
     while ((b0<bn)&&(b0<vmconf[i].vmpcfg[j].blks)) {
       unsigned b1=frame_splitter(
         vmconf[i].vmpcfg[j].blk,b0,
         vmconf[i].vmpcfg[j].blks,
         vmconf[i].vmpcfg[j].blk[b0].posx,
         vmconf[i].vmpcfg[j].blk[b0].posy,
         vmconf[i].vmpcfg[j].blk[b0].width,
         vmconf[i].vmpcfg[j].blk[b0].height,
         vmconf[i].vmpcfg[k].blk[0].posx,
         vmconf[i].vmpcfg[k].blk[0].posy,
         vmconf[i].vmpcfg[k].blk[0].width,
         vmconf[i].vmpcfg[k].blk[0].height
       );
//       if(b1==vmconf[i].vmpcfg[j].blks)b0++;
       if(!geometry_changed)b0++;
       else vmconf[i].vmpcfg[j].blks=b1;
     }
   }
 }

}

void VideoMixConfigurator::handle_line(char* &f_buff,long pos,long pos1,long line){
   long i,pos0=pos;
   bool escape=false;
   for(i=pos;i<pos1;i++) {
    escape=((f_buff[i]=='\\')&&(!escape));
    if(!escape){
     if(f_buff[i]=='#') { // comment
      if(pos!=i)handle_atom(f_buff,pos,i,line,pos-pos0);
	  return;
	 }
	 if(f_buff[i]=='/') if(i<pos1-1) if(f_buff[i+1]=='/') { // comment
      if(pos!=i)handle_atom(f_buff,pos,i,line,pos-pos0);
	  return;
	 }
     if(f_buff[i]==';') {
      if(i!=pos)handle_atom(f_buff,pos,i,line,pos-pos0);
      pos=i+1;
     }
    }
   }
   if(pos!=pos1)handle_atom(f_buff,pos,pos1,line,pos-pos0);
  }

void VideoMixConfigurator::handle_atom(char* &f_buff,long pos,long pos1,long line,long lo){
   while((f_buff[pos]<33)&&(pos<pos1)) {pos++; lo++;}
   if(pos==pos1) return; //empty atom
   while(f_buff[pos1-1]<33) pos1--; // atom is now trimed: [pos..pos1-1]
   if(f_buff[pos]=='[') handle_layout_descriptor(f_buff,pos,pos1,line,lo);
   else if(f_buff[pos]=='(') handle_position_descriptor(f_buff,pos,pos1,line,lo);
   else handle_parameter(f_buff,pos,pos1,line,lo);
  }

void VideoMixConfigurator::handle_layout_descriptor(char* &f_buff,long pos,long pos1,long line,long lo){
   if(f_buff[pos1-1]==']'){
    pos++;
    pos1--;
    if(pos1-pos>32) {
     warning(f_buff,line,lo,"too long layout id, truncated",pos,pos+32);
     pos1=pos+32;
    };
    if(ldm==1)finalize_layout_desc();
    if(pos1<=pos) return; //empty layout Id
    initialize_layout_desc(f_buff,pos,pos1,line,lo);
   } else warning(f_buff,line,lo,"bad token",pos,pos1);
//   cout << line << "/" << lo << "\tLAYOUT\t"; for (long i=pos;i<pos1;i++) cout << (char)f_buff[i]; cout << "\n";
  }

void VideoMixConfigurator::handle_position_descriptor(char* &f_buff,long pos,long pos1,long line,long lo){
   if(f_buff[pos1-1]==')'){
    pos++; pos1--;
    while((f_buff[pos]<33)&&(pos<pos1))pos++;
    if(pos==pos1) { warning(f_buff,line,lo,"incomplete position descriptor",1,0); return; }
    if((f_buff[pos]>'9')||(f_buff[pos]<'0')){ warning(f_buff,line,lo,"error in position description",pos,pos1); return; }
    long pos0=pos;
    while((pos<pos1)&&(f_buff[pos]>='0')&&(f_buff[pos]<='9'))pos++;
    long pos2=pos;
    while((pos<pos1)&&(f_buff[pos]<33))pos++;
    if(f_buff[pos]!=','){ warning(f_buff,line,lo,"unknown character in position descriptor",pos,pos1); return; }
    char t=f_buff[pos2];
    f_buff[pos2]=0;
    opts[1].posx=(atoi((const char*)(f_buff+pos0))*bfw)/fw[1];
//    opts[1].posx=((atoi((const char*)(f_buff+pos0))*bfw)/fw[1]+1)&0xFFFFFE;
    f_buff[pos2]=t;
    pos++;
    if(pos>=pos1){ warning(f_buff,line,lo,"Y-coordinate does not set",1,0); return; }
    while((f_buff[pos]<33)&&(pos<pos1))pos++;
    if(pos==pos1) { warning(f_buff,line,lo,"Y not set",1,0); return; }
    if((f_buff[pos]>'9')||(f_buff[pos]<'0')){ warning(f_buff,line,lo,"error in Y crd. description",pos,pos1); return; }
    pos0=pos;
    while((pos<pos1)&&(f_buff[pos]>='0')&&(f_buff[pos]<='9'))pos++;
    pos2=pos;
    while((pos<pos1)&&(f_buff[pos]<33))pos++;
    if(pos!=pos1){ warning(f_buff,line,lo,"unknown chars in position descriptor",pos,pos1); return; }
    t=f_buff[pos2];
    f_buff[pos2]=0;
    opts[1].posy=(atoi((const char*)(f_buff+pos0))*bfh)/fh[1];
//    opts[1].posy=((atoi((const char*)(f_buff+pos0))*bfh)/fh[1]+1)&0xFFFFFE;
    f_buff[pos2]=t;
   } else warning(f_buff,line,lo,"bad position descriptor",pos,pos1);
   if(pos_n==0)vmconf[lid].vmpcfg=(VMPCfgOptions *)malloc(sizeof(VMPCfgOptions));
   else vmconf[lid].vmpcfg=(VMPCfgOptions *)realloc((void *)(vmconf[lid].vmpcfg),sizeof(VMPCfgOptions)*(pos_n+1));
   vmconf[lid].vmpcfg[pos_n]=opts[1];
//   cout << "Position " << pos_n << ": " << opts[true].posx << "," << opts[true].posy << "\n";
   pos_n++;
  }

void VideoMixConfigurator::handle_parameter(char* &f_buff,long pos,long pos1,long line,long lo){
   char p[64], v[256];
   bool escape=false;
   long pos0=pos;
   long pos00=pos;
   while((pos<pos1)&&(((f_buff[pos]>32)&&(f_buff[pos]!='='))||escape)&&(pos-pos0<63)){
    p[pos-pos0]=f_buff[pos];
    pos++;
   }
   p[pos-pos0]=0;
   if(pos-pos0>63){ warning(f_buff,line,lo,"parameter name is too long",pos0,pos1); return; }
   while((pos<pos1)&&(f_buff[pos]<33))pos++;
   if(pos==pos1){ warning(f_buff,line,lo,"unknown text",pos0,pos1); return; }
   if(f_buff[pos]!='='){ warning(f_buff,line,lo,"missing \"=\"",pos0,pos1); return; }
   pos++;
   while((pos<pos1)&&(f_buff[pos]<33))pos++;
   escape=false;
   pos0=pos;
   while((pos<pos1)&&((f_buff[pos]>31)||escape)&&(pos-pos0<255)){
    v[pos-pos0]=f_buff[pos];
    pos++;
   }
   v[pos-pos0]=0;
   if(pos-pos0>255){ warning(f_buff,line,lo,"parameter value is too long",pos0,pos1); return; }
   while((pos<pos1)&&(f_buff[pos]<33))pos++;
   if(pos!=pos1) warning(f_buff,line,lo,"unknown characters",pos,pos1);
   option_set((const char *)p,(const char *)v, f_buff, line, lo, pos00, pos0);
  }

void VideoMixConfigurator::option_set(const char* p, const char* v, char* &f_buff, long line, long lo, long pos, long pos1){
   if(option_cmp((const char *)p,(const char *)"frame_width")){fw[ldm]=atoi(v);if(fw[ldm]<1)fw[ldm]=1;}
   else if(option_cmp((const char *)p,(const char *)"frame_height")){fh[ldm]=atoi(v);if(fh[ldm]<1)fh[ldm]=1;}
   else if(option_cmp((const char *)p,(const char *)"border"))opts[ldm].border=atoi(v);
   else if(option_cmp((const char *)p,(const char *)"mode_mask"))sopts[ldm].mode_mask=atoi(v);
   else if(option_cmp((const char *)p,(const char *)"tags")){
     if(strlen(v)<=128)strcpy(sopts[ldm].tags,v); else 
     warning(f_buff,line,lo,"tags value too long (max 128 chars allowed)",pos,pos1);
   }
   else if(option_cmp((const char *)p,(const char *)"position_width")) opts[ldm].width=(atoi(v)*bfw)/fw[ldm];
//   else if(option_cmp((const char *)p,(const char *)"position_width")) opts[ldm].width=((atoi(v)*bfw)/fw[ldm]+1)&0xFFFFFE;
   else if(option_cmp((const char *)p,(const char *)"position_height")) opts[ldm].height=(atoi(v)*bfh)/fh[ldm];
//   else if(option_cmp((const char *)p,(const char *)"position_height")) opts[ldm].height=((atoi(v)*bfh)/fh[ldm]+1)&0xFFFFFE;
#if USE_FREETYPE
   else if(option_cmp((const char *)p,(const char *)"label_mask")) opts[ldm].label_mask=atoi(v);
   else if(option_cmp((const char *)p,(const char *)"label_color")) {
    int tempc=0xFFFFFF; sscanf(v,"%x",&tempc); int R=(tempc>>16)&255; int G=(tempc>>8)&255; int B=tempc&255;
    int U = (BYTE)PMIN(ABS(R * -1214 + G * -2384 + B * 3598 + 4096 + 1048576) / 8192, 240);
    int V = (BYTE)PMIN(ABS(R *  3598 + G * -3013 + B * -585 + 4096 + 1048576) / 8192, 240);
    opts[ldm].label_color=(U<<8)+V;
   }
   else if(option_cmp((const char *)p,(const char *)"label_bgcolor")) {
    int tempc=0xFFFFFF; sscanf(v,"%x",&tempc); int R=(tempc>>16)&255; int G=(tempc>>8)&255; int B=tempc&255;
    int U = (BYTE)PMIN(ABS(R * -1214 + G * -2384 + B * 3598 + 4096 + 1048576) / 8192, 240);
    int V = (BYTE)PMIN(ABS(R *  3598 + G * -3013 + B * -585 + 4096 + 1048576) / 8192, 240);
    opts[ldm].label_bgcolor=(U<<8)+V;
   }
   else if(option_cmp((const char *)p,(const char *)"font")) {
    if(strlen(v)<256)strcpy(fontfile,v); else 
    warning(f_buff,line,lo,"fonts value too long (max 255 chars allowed)",pos,pos1);
   }
   else if(option_cmp((const char *)p,(const char *)"fontsize")) {
    if(strlen(v)<11)strcpy(opts[ldm].fontsize,v); else 
    warning(f_buff,line,lo,"fontsize value too long (max 10 chars allowed)",pos,pos1);
   }
   else if(option_cmp((const char *)p,(const char *)"border_left")) {
    if(strlen(v)<11)strcpy(opts[ldm].border_left,v); else 
    warning(f_buff,line,lo,"border_left value too long (max 10 chars allowed)",pos,pos1);
   }
   else if(option_cmp((const char *)p,(const char *)"border_right")) {
    if(strlen(v)<11)strcpy(opts[ldm].border_right,v); else 
    warning(f_buff,line,lo,"border_right value too long (max 10 chars allowed)",pos,pos1);
   }
   else if(option_cmp((const char *)p,(const char *)"border_top")) {
    if(strlen(v)<11)strcpy(opts[ldm].border_top,v); else 
    warning(f_buff,line,lo,"border_top value too long (max 10 chars allowed)",pos,pos1);
   }
   else if(option_cmp((const char *)p,(const char *)"border_bottom")) {
    if(strlen(v)<11)strcpy(opts[ldm].border_bottom,v); else 
    warning(f_buff,line,lo,"border_bottom value too long (max 10 chars allowed)",pos,pos1);
   }
   else if(option_cmp((const char *)p,(const char *)"cut_before_bracket")) opts[ldm].cut_before_bracket=atoi(v);
#endif
   else if(option_cmp((const char *)p,(const char *)"reallocate_on_disconnect")) sopts[ldm].reallocate_on_disconnect=atoi(v);
   else if(option_cmp((const char *)p,(const char *)"new_members_first")) sopts[ldm].new_from_begin=atoi(v);
   else if(option_cmp((const char *)p,(const char *)"mockup_width")) sopts[ldm].mockup_width=atoi(v);
   else if(option_cmp((const char *)p,(const char *)"mockup_height")) sopts[ldm].mockup_height=atoi(v);
   else warning(f_buff,line,lo,"unknown parameter",pos,pos1);
  }

bool VideoMixConfigurator::option_cmp(const char* p,const char* str){
   if(strlen(p)!=strlen(str))return false;
   for(unsigned i=0;i<strlen(str);i++)if(p[i]!=str[i]) return false;
   return true;
  }

void VideoMixConfigurator::warning(char* &f_buff,long line,long lo,const char warn[64],long pos,long pos1){
   cout << "Warning! " << VMPC_CONFIGURATION_NAME << ":" << line << ":" << lo << ": "<< warn;
   if(pos1>pos) {
    cout << ": \"";
    for(long i=pos;i<pos1;i++) cout << (char)f_buff[i];
    cout << "\"";
   }
   cout << "\n";
  }

void VideoMixConfigurator::initialize_layout_desc(char* &f_buff,long pos,long pos1,long line,long lo){
   ldm=1;
   pos_n=0;
   opts[1]=opts[0]; sopts[1]=sopts[0]; fw[1]=fw[0]; fh[1]=fh[0];
   f_buff[pos1]=0;
//cout << " memcpy " << (pos1-pos+1) << "\n" << sopts[true].Id << "\n" << f_buff[pos] << "\n";
   strcpy((char *)(sopts[1].Id),(char *)(f_buff+pos));
   f_buff[pos1]=']';
   if(lid==0)vmconf=(VMPCfgLayout *)malloc(sizeof(VMPCfgLayout));
   else vmconf=(VMPCfgLayout *)realloc((void *)vmconf,sizeof(VMPCfgLayout)*(lid+1));
   vmconf[lid].vmpcfg=NULL;
  }

void VideoMixConfigurator::finalize_layout_desc(){
   ldm=0;
   vmconf[lid].splitcfg=sopts[1];
   vmconf[lid].splitcfg.vidnum=pos_n;
   lid++;
   vmconfs=lid;
  }

#endif // OPENMCU_VIDEO
