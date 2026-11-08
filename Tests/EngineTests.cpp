// Epsilon Carve engine tests: evenness, Collision behaviour, Width Guard, bit-exactness, safety, CPU.
#include "CarveEngine.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <vector>
#include <cmath>
#include <cstdio>
#include <random>
#include <chrono>
#include <cstring>
using namespace carve;
int fails=0;
#define CHECK(c,msg,...) do{bool ok_=(c);printf("[%s] " msg "\n",ok_?"PASS":"FAIL",__VA_ARGS__);if(!ok_)++fails;}while(0)
static double rms(const float* v,int n){double s=0;for(int i=0;i<n;++i)s+=double(v[i])*v[i];return std::sqrt(s/n);}
struct Sig{std::vector<float>L,R;};
Sig sine(double sr,int n,float f,float a){Sig s{std::vector<float>(n),std::vector<float>(n)};for(int i=0;i<n;++i)s.L[i]=s.R[i]=a*std::sin(2*M_PI*f*i/sr);return s;}
Sig noise(int n,float a,unsigned seed,bool corr=false){Sig s{std::vector<float>(n),std::vector<float>(n)};std::mt19937 r(seed);std::normal_distribution<float>d(0,a);for(int i=0;i<n;++i){s.L[i]=d(r);s.R[i]=corr?s.L[i]:d(r);}return s;}
void run(Engine&e,Sig&t,const Sig&s,int ch,int sch,Settings st,int block=512){int n=t.L.size();for(int p=0;p<n;p+=block){int m=std::min(block,n-p);Engine::TargetChannels tc{t.L.data()+p,ch>1?t.R.data()+p:nullptr};Engine::TriggerChannels sc{sch>0?s.L.data()+p:nullptr,sch>1?s.R.data()+p:nullptr};e.process(tc,ch,sc,sch,m,st);}}
double cutDb(double sr,const Sig& in,const Sig& out){int n=in.L.size();return 20*std::log10(rms(out.L.data()+n/2,n/2)/rms(in.L.data()+n/2,n/2));}
int main(){
 Settings classic; classic.mode=Mode::classic; classic.amountPercent=100;
 Settings coll; coll.mode=Mode::collision; coll.amountPercent=100;
 printf("== Classic, tone trigger = probe (evenness across pitch)\n");
 for(double sr:{44100.0,48000.0,96000.0}){double mn=0,mx=-99;
  for(float f=55;f<0.42*sr&&f<19000;f*=1.0905f){int n=int(sr*1.5);auto t=sine(sr,n,f,0.25f),s=sine(sr,n,f,0.5f);auto o=t;Engine e;e.prepare(sr);run(e,o,s,1,1,classic);double c=cutDb(sr,t,o);mn=std::min(mn,c);mx=std::max(mx,c);}
  CHECK(mx-mn<6.5 && mn>-22,"sr %.0f: tone cut range %.2f .. %.2f dB (spread %.2f dB, v0.3.0 was ~11 dB)",sr,mn,mx,mx-mn);}
 printf("== Classic broadband: noise trigger + noise target at 100%%\n");
 {double sr=48000;int n=96000;auto t=noise(n,0.1f,1),s=noise(n,0.2f,2);auto o=t;Engine e;e.prepare(sr);run(e,o,s,2,2,classic);double c=cutDb(sr,t,o);CHECK(c<-3&&c>-20,"broadband cut %.2f dB",c);}
 printf("== Collision: trigger 1 kHz tone at -12 dBFS, target noise at varying level\n");
 {double sr=48000;int n=96000;double prev=1;bool mono=true;for(float a:{0.003f,0.01f,0.03f,0.1f,0.3f}){auto t=noise(n,a,3),s=sine(sr,n,1000,0.25f);auto o=t;Engine e;e.prepare(sr);run(e,o,s,2,2,coll);
   // measure target energy near 1k by filtering? use whole-band rms: small change expected; instead measure with a 1k probe tone mixed
   auto tp=sine(sr,n,1000,a);auto op=tp;Engine e2;e2.prepare(sr);run(e2,op,s,2,2,coll);double c=cutDb(sr,tp,op);printf("   target tone %.3f (%.1f dBFS): cut at 1 kHz %.2f dB\n",a,20*log10(a),c);if(c>prev+0.05)mono=false;prev=c;}
  CHECK(mono,"cut deepens monotonically as the target gets louder%s","");}
 {double sr=48000;int n=96000;auto tp=sine(sr,n,1000,0.003f),s=sine(sr,n,1000,0.25f);auto op=tp;Engine e;e.prepare(sr);run(e,op,s,2,2,coll);double c=cutDb(sr,tp,op);CHECK(c>-0.5,"quiet target under loud trigger is left alone (%.2f dB)",c);}
 {double sr=48000;int n=96000;auto tp=sine(sr,n,200,0.3f),s=sine(sr,n,3000,0.25f);auto op=tp;Engine e;e.prepare(sr);run(e,op,s,2,2,coll);double c=cutDb(sr,tp,op);CHECK(c>-0.6,"no collision (target 200 Hz, trigger 3 kHz): %.2f dB",c);}
 printf("== Width Guard\n");
 {double sr=48000;int n=96000;auto t=noise(n,0.2f,4),s=noise(n,0.2f,5);
  for(bool g:{false,true}){auto o=t;Settings st=classic;st.widthGuard=g;Engine e;e.prepare(sr);run(e,o,s,2,2,st);
   std::vector<float>Si(n),So(n),Mi(n),Mo(n);for(int i=0;i<n;++i){Si[i]=.5f*(t.L[i]-t.R[i]);So[i]=.5f*(o.L[i]-o.R[i]);Mi[i]=.5f*(t.L[i]+t.R[i]);Mo[i]=.5f*(o.L[i]+o.R[i]);}
   double sd=20*log10(rms(So.data()+n/2,n/2)/rms(Si.data()+n/2,n/2)),md=20*log10(rms(Mo.data()+n/2,n/2)/rms(Mi.data()+n/2,n/2));
   if(g) CHECK(std::abs(sd)<0.01&&md<-3,"guard on: mid %.2f dB, side %.3f dB",md,sd); else CHECK(sd<-3,"guard off: mid %.2f dB, side %.2f dB",md,sd);}}
 printf("== Bit-exact and safety\n");
 for(auto st:{classic,coll}){double sr=44100;int n=44100;auto t=noise(n,0.3f,6),s=noise(n,0.3f,7);auto o=t;st.amountPercent=0;Engine e;e.prepare(sr);run(e,o,s,2,2,st);int d=0;for(int i=0;i<n;++i)d+=(o.L[i]!=t.L[i])+(o.R[i]!=t.R[i]);CHECK(d==0,"%s Amount 0: %d differing samples",st.mode==Mode::classic?"classic":"collision",d);}
 {double sr=44100;int n=44100;auto t=noise(n,0.3f,6);Sig s{std::vector<float>(n),std::vector<float>(n)};auto o=t;Engine e;e.prepare(sr);run(e,o,s,2,2,coll);int d=0;for(int i=0;i<n;++i)d+=(o.L[i]!=t.L[i]);CHECK(d==0,"collision, silent sidechain: %d differing samples",d);}
 {double sr=48000;int n=48000;auto t=noise(n,0.2f,8),s=noise(n,0.2f,9);auto a=t,b=t;Engine e1,e2;e1.prepare(sr);e2.prepare(sr);run(e1,a,s,2,2,coll,64);run(e2,b,s,2,2,coll,997);int d=0;for(int i=0;i<n;++i)d+=(a.L[i]!=b.L[i]);CHECK(d==0,"block size 64 vs 997 identical (%d diffs)",d);}
 {double sr=48000;int n=48000;auto t=noise(n,50.f,10),s=noise(n,50.f,11);for(auto st:{classic,coll}){auto o=t;Engine e;e.prepare(sr);run(e,o,s,2,2,st);bool f=true;float pk=0;for(int i=0;i<n;++i){f&=std::isfinite(o.L[i])&&std::isfinite(o.R[i]);pk=std::max(pk,std::abs(o.L[i]));}CHECK(f&&pk<400,"+34 dBFS input stays finite (peak %.1f)",pk);}}
 {double sr=22050;int n=22050;auto t=noise(n,.2f,1),s=noise(n,.2f,2);Engine e;e.prepare(sr);run(e,t,s,1,2,coll);bool f=true;for(float x:t.L)f&=std::isfinite(x);CHECK(f,"22.05 kHz mono target, stereo trigger finite%s","");}
 {double sr=192000;int n=96000;auto t=noise(n,.2f,1),s=noise(n,.2f,2);Engine e;e.prepare(sr);run(e,t,s,2,1,classic);bool f=true;for(float x:t.L)f&=std::isfinite(x);CHECK(f,"192 kHz stereo target, mono trigger finite%s","");}
 {double sr=48000;int n=48000;auto t=noise(n,.2f,1);Sig none;Engine e;e.prepare(sr);Settings st=coll;auto o=t;for(int p=0;p<n;p+=256){Engine::TargetChannels tc{o.L.data()+p,o.R.data()+p};e.process(tc,2,{},0,std::min(256,n-p),st);}double c=cutDb(sr,t,o);bool f=true;for(float x:o.L)f&=std::isfinite(x);CHECK(f,"no sidechain bus -> self (classic) fallback, cut %.2f dB",c);}
 printf("== CPU (48 kHz stereo, 256-sample blocks)\n");
 for(auto st:{classic,coll}){double sr=48000;int n=48000*10;auto t=noise(n,.3f,1),s=noise(n,.3f,2);Engine e;e.prepare(sr);e.setMeteringEnabled(true);MeterFrame fr;auto t0=std::chrono::steady_clock::now();for(int p=0;p<n;p+=256){Engine::TargetChannels tc{t.L.data()+p,t.R.data()+p};Engine::TriggerChannels sc{s.L.data()+p,s.R.data()+p};e.process(tc,2,sc,2,std::min(256,n-p),st);e.readMeterFrame(fr);}double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();printf("   %s: %.2f%% of one core\n",st.mode==Mode::classic?"classic":"collision",100*sec/10);}
 printf("\n%d failure(s)\n",fails);return fails;}
