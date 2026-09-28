/* sdl_drain_monitor: is the host audio device draining at real time?
 *
 * Opens SDL queued audio exactly like port/src/audio.c (22050 Hz, S16,
 * stereo, 512 samples), keeps the queue deep with silence, and measures the
 * drain rate per 1 s window against the performance counter. No game, no
 * window. Windows of +-2.3% are one 512-frame pull landing either side of a
 * window edge (granularity, ignore); a sustained run of lower values means the
 * device/driver/audio engine is falling behind, and the game's audio queue
 * will overflow (D322: a Focusrite USB interface dipped to 88% for ~15 s
 * every ~312 s, the whole "garbled audio" signature, with no game running).
 *
 * Build (MSYS2 MINGW64):
 *   gcc -O2 tools_pc/sdl_drain_monitor.c -o sdl_drain_monitor.exe  *       $(sdl2-config --cflags --libs | sed 's/-mwindows//') -lwinmm
 * Run: ./sdl_drain_monitor.exe [seconds, default 660]
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <windows.h>
static SDL_AudioDeviceID d; static volatile long long pushed=0; static volatile int run=1;
static int prod(void*a){(void)a; static Sint16 b[735*2];
  while(run){ if(SDL_GetQueuedAudioSize(d)/4<8000){SDL_QueueAudio(d,b,735*4); pushed+=735;} SDL_Delay(5);} return 0;}
int main(int c,char**v){ int secs=c>1?atoi(v[1]):660; timeBeginPeriod(1); SDL_Init(SDL_INIT_AUDIO);
  SDL_AudioSpec w={0},h; w.freq=22050;w.format=AUDIO_S16SYS;w.channels=2;w.samples=512;
  d=SDL_OpenAudioDevice(NULL,0,&w,&h,0);
  printf("device=%s driver=%s\n",SDL_GetAudioDeviceName(0,0),SDL_GetCurrentAudioDriver()); fflush(stdout);
  SDL_PauseAudioDevice(d,0); SDL_Thread*t=SDL_CreateThread(prod,"p",0); SDL_Delay(1000);
  Uint64 f=SDL_GetPerformanceFrequency(), t0=SDL_GetPerformanceCounter(), tp=t0;
  long long p0=pushed; Uint32 q0=SDL_GetQueuedAudioSize(d)/4; double mn=1e9,mx=0,sum=0; int n=0,bad=0;
  for(int s=1;s<=secs;s++){ SDL_Delay(1000);
    Uint64 now=SDL_GetPerformanceCounter(); long long p=pushed; Uint32 q=SDL_GetQueuedAudioSize(d)/4;
    double el=(double)(now-tp)/f, rate=((double)(p-p0)+(double)q0-(double)q)/el;
    if(rate<mn)mn=rate; if(rate>mx)mx=rate; sum+=rate;n++;
    if(rate<22002*0.98||rate>22002*1.02){bad++; printf("t=%ds drain=%.0f frames/s (%.1f%%) q=%u\n",s,rate,100*rate/22002,q); fflush(stdout);}
    tp=now;p0=p;q0=q; }
  printf("done %ds: avg=%.1f min=%.0f max=%.0f anomalous_windows=%d\n",n,sum/n,mn,mx,bad);
  run=0; SDL_WaitThread(t,0); return 0;}
