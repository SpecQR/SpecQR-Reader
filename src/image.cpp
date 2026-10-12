#include "specqr/reader.hpp"
#include "qr_tables.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <utility>

namespace specqr { namespace {
// These ceilings keep a caller-provided Limits object from disabling the
// implementation's allocation/work bounds. No image or codec state is global.
constexpr std::size_t hard_pixels=64u*1024u*1024u, hard_dimension=32768;
constexpr std::size_t hard_candidates=128, hard_attempts=16384;
struct Raster {
    int width=0,height=0;
    std::vector<std::uint8_t> pixels;
    bool at(int x,int y) const { return x>=0 && y>=0 && x<width && y<height && pixels[std::size_t(y)*width+x]!=0; }
};
struct Finder { double x=0,y=0,module=0; unsigned hits=0; };
struct Hypothesis { Finder tl,tr,bl; double rank=0; };
struct Map {
    Point tl,tr,bl; double span=1,g=0,h=0;
    bool point(double x,double y,Point& out) const {
        const double u=(x-3.5)/span,v=(y-3.5)/span,d=1+g*u+h*v;
        if (!std::isfinite(d) || d<0.15) return false;
        out.x=(tl.x+u*((1+g)*tr.x-tl.x)+v*((1+h)*bl.x-tl.x))/d;
        out.y=(tl.y+u*((1+g)*tr.y-tl.y)+v*((1+h)*bl.y-tl.y))/d;
        return std::isfinite(out.x)&&std::isfinite(out.y);
    }
};
struct Budget { std::size_t attempts=0,maximum=0; std::uint64_t probes=0,probe_max=0; bool clipped=false; };
static double distance(const Finder& a,const Finder& b) { return std::hypot(a.x-b.x,a.y-b.y); }
static double module_dimension(const Finder& a,const Finder& b) {
    // A horizontal/vertical cut through a rotated finder measures a longer
    // chord than the side length. Project back onto its inferred symbol axis.
    const double dx=std::abs(a.x-b.x),dy=std::abs(a.y-b.y),length=std::hypot(dx,dy);
    const double orientation=std::max(dx,dy)/length;
    return length/(((a.module+b.module)*0.5)*orientation);
}
static bool multiply(std::size_t a,std::size_t b,std::size_t& out) {
    if (b && a>std::numeric_limits<std::size_t>::max()/b) return false;
    out=a*b; return true;
}
static bool ratio(const std::array<int,5>& runs,double& module) {
    const int total=std::accumulate(runs.begin(),runs.end(),0);
    if(total<7) return false;
    module=total/7.0;
    // A cut through a rotated square changes the outer runs most strongly.
    for(int i=0;i<5;++i) {
        const double target=(i==2?3:1)*module;
        if(runs[i]<=0 || std::abs(runs[i]-target)>(i==2?1.30:0.85)*module) return false;
    }
    return true;
}
static bool cross(const Raster& r,int x,int y,int dx,int dy,double expected,double& center,double& module) {
    if(!r.at(x,y)) return false;
    const int cap=std::min(4096,std::max(5,int(expected*5+3)));
    std::array<int,5> runs{};
    int k=0;
    while(k<cap && r.at(x-k*dx,y-k*dy)) {++runs[2];++k;}
    while(k<cap && !r.at(x-k*dx,y-k*dy) && x-k*dx>=0 && y-k*dy>=0 && x-k*dx<r.width && y-k*dy<r.height) {++runs[1];++k;}
    while(k<cap && r.at(x-k*dx,y-k*dy)) {++runs[0];++k;}
    int p=1;
    while(p<cap && r.at(x+p*dx,y+p*dy)) {++runs[2];++p;}
    while(p<cap && !r.at(x+p*dx,y+p*dy) && x+p*dx>=0 && y+p*dy>=0 && x+p*dx<r.width && y+p*dy<r.height) {++runs[3];++p;}
    while(p<cap && r.at(x+p*dx,y+p*dy)) {++runs[4];++p;}
    if(!ratio(runs,module) || module<expected*0.45 || module>expected*2.2) return false;
    center=(dx?x:y)+(p-runs[4]-runs[3])-runs[2]/2.0-0.5;
    return true;
}
static void add_finder(std::vector<Finder>& found,Finder f,std::size_t cap,bool& clipped) {
    for(auto& old:found) {
        if(std::hypot(old.x-f.x,old.y-f.y)<std::max(2.0,std::min(old.module,f.module)*1.7)
            && std::max(old.module,f.module)<std::min(old.module,f.module)*2.2) {
            const double count=old.hits;
            old.x=(old.x*count+f.x)/(count+1);old.y=(old.y*count+f.y)/(count+1);
            old.module=(old.module*count+f.module)/(count+1);++old.hits; return;
        }
    }
    if(found.size()<cap) found.push_back(f);
    else {
        clipped=true;
        auto weakest=std::min_element(found.begin(),found.end(),[](const Finder& a,const Finder& b){return a.hits<b.hits;});
        if(weakest->hits<2)*weakest=f;
    }
}
static std::vector<Finder> finders(const Raster& r,std::size_t cap,bool& clipped) {
    std::vector<Finder> found;
    found.reserve(cap);
    // Every scanline is visited: this also supports modules only two pixels wide.
    for(int y=0;y<r.height;++y) {
        std::array<int,5> runs{}; int count=0; bool prior=false;
        for(int x=0;x<=r.width;++x) {
            const bool black=x<r.width && r.at(x,y);
            if(x==0) {prior=black;runs[4]=1;count=1;continue;}
            if(black==prior && x<r.width) {++runs[4];continue;}
            if(prior && count>=5) {
                double m=0;
                if(ratio(runs,m)) {
                    const double cx=x-runs[4]-runs[3]-runs[2]/2.0-0.5;
                    double cy=0,mv=0,refined=0,mh=0;
                    if(cross(r,int(std::round(cx)),y,0,1,m,cy,mv)
                       && cross(r,int(std::round(cx)),int(std::round(cy)),1,0,m,refined,mh))
                        add_finder(found,{refined,cy,(mh+mv)/2,1},cap,clipped);
                }
            }
            for(int k=0;k<4;++k) runs[k]=runs[k+1];
            runs[4]=1;++count;prior=black;
        }
    }
    found.erase(std::remove_if(found.begin(),found.end(),[](const Finder& f){return f.hits<2;}),found.end());
    std::sort(found.begin(),found.end(),[](const Finder&a,const Finder&b){return a.hits>b.hits;});
    return found;
}
static std::vector<Hypothesis> hypotheses(const std::vector<Finder>& f,bool& clipped) {
    std::vector<Hypothesis> out; out.reserve(256);
    const auto lower_rank=[](const Hypothesis& a,const Hypothesis& b){return a.rank<b.rank;};
    for(std::size_t i=0;i<f.size();++i) for(std::size_t j=i+1;j<f.size();++j) for(std::size_t k=j+1;k<f.size();++k) {
        const std::array<Finder,3> set={f[i],f[j],f[k]};
        for(int t=0;t<3;++t) {
            Finder a=set[t],b=set[(t+1)%3],c=set[(t+2)%3];
            const double ab=distance(a,b),ac=distance(a,c);
            const double small=std::min({a.module,b.module,c.module}),large=std::max({a.module,b.module,c.module});
            if(small<0.7 || large>small*2.8 || std::min(ab,ac)<small*10 || std::max(ab,ac)>std::min(ab,ac)*2.7) continue;
            const double cosine=((b.x-a.x)*(c.x-a.x)+(b.y-a.y)*(c.y-a.y))/(ab*ac);
            if(std::abs(cosine)>0.85) continue;
            if((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)<0) std::swap(b,c);
            const double dim=(module_dimension(a,b)+module_dimension(a,c))*0.5+7;
            if(dim<17 || dim>195) continue;
            const double rank=std::abs(cosine)*3+std::abs(std::log(ab/ac))+std::log(large/small)
                -std::log(1.0+std::min({a.hits,b.hits,c.hits}))*0.25;
            if(out.size()<256) {
                out.push_back({a,b,c,rank});std::push_heap(out.begin(),out.end(),lower_rank);
            } else {
                clipped=true;
                if(rank<out.front().rank) {
                    std::pop_heap(out.begin(),out.end(),lower_rank);out.back()={a,b,c,rank};
                    std::push_heap(out.begin(),out.end(),lower_rank);
                }
            }
        }
    }
    std::sort(out.begin(),out.end(),[](const Hypothesis&a,const Hypothesis&b){return a.rank<b.rank;});
    return out;
}
static bool read_point(const Raster& r,const Map& map,double x,double y,bool& black) {
    Point p;
    if(!map.point(x,y,p) || p.x<-.5 || p.y<-.5 || p.x>=r.width-.5 || p.y>=r.height-.5) return false;
    black=r.at(int(std::floor(p.x+.5)),int(std::floor(p.y+.5)));return true;
}
static double pattern_score(const Raster& r,const Map& map,int n,Budget& budget) {
    unsigned wrong=0,total=0;
    auto check=[&](int x,int y,bool expected) {
        for(const auto& offset:std::array<Point,5>{{{0,0},{-.27,0},{.27,0},{0,-.27},{0,.27}}}) {
            ++total;bool actual=false;
            if(!read_point(r,map,x+.5+offset.x,y+.5+offset.y,actual)||actual!=expected) ++wrong;
        }
    };
    for(const auto& origin:std::array<std::pair<int,int>,3>{{{0,0},{n-7,0},{0,n-7}}}) {
        for(int y=-1;y<=7;++y) for(int x=-1;x<=7;++x) {
            if(origin.first+x<0||origin.second+y<0||origin.first+x>=n||origin.second+y>=n)continue;
            const bool inside=x>=0&&x<7&&y>=0&&y<7;
            const bool expected=inside && (x==0||x==6||y==0||y==6||(x>=2&&x<=4&&y>=2&&y<=4));
            check(origin.first+x,origin.second+y,expected);
        }
    }
    for(int t=8;t<n-8;++t) {check(t,6,(t&1)==0);check(6,t,(t&1)==0);}
    if(n>21) for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x) check(n-7+x,n-7+y,std::abs(x)==2||std::abs(y)==2||(x==0&&y==0));
    budget.probes+=total;
    return total?double(wrong)/total:1;
}
static bool admissible(const Map& m) { return m.g>=-.65&&m.g<=1.2&&m.h>=-.65&&m.h<=1.2&&1+m.g+m.h>.3; }
static Map refine(const Raster& r,Map initial,int n,Budget& budget,double& score) {
    Map best=initial;score=pattern_score(r,best,n,budget);
    for(double step: {0.16,0.08,0.04,0.02,0.01}) {
        for(int iteration=0;iteration<3;++iteration) {
            Map next=best;double next_score=score;
            for(int dx=-1;dx<=1;++dx) for(int dy=-1;dy<=1;++dy) {
                if((!dx&&!dy)||budget.probes>=budget.probe_max) continue;
                Map m=best;m.g+=dx*step;m.h+=dy*step;
                if(!admissible(m))continue;
                const double s=pattern_score(r,m,n,budget);
                if(s+1e-9<next_score) {next=m;next_score=s;}
            }
            if(next_score>=score)break;
            best=next;score=next_score;
        }
    }
    return best;
}
static double alignment_score(const Raster& r,const Map& map,int n,Budget& budget) {
    unsigned wrong=0,total=0;
    const auto& centers=detail::alignment_centers[(n-21)/4];
    for(int cy:centers)for(int cx:centers) {
        if(!cx||!cy||(cx==6&&(cy==6||cy==n-7))||(cy==6&&cx==n-7))continue;
        for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x) {
            const bool expected=std::abs(x)==2||std::abs(y)==2||(x==0&&y==0);
            for(const auto& offset:std::array<Point,5>{{{0,0},{-.27,0},{.27,0},{0,-.27},{0,.27}}}) {
                ++total;bool actual=false;
                if(!read_point(r,map,cx+x+.5+offset.x,cy+y+.5+offset.y,actual)||actual!=expected)++wrong;
            }
        }
    }
    budget.probes+=total;
    return total?double(wrong)/total:1;
}
static Map refine_centers(const Raster& r,Map initial,int n,Budget& budget,double& score) {
    // Finder scanline centers can be biased by perspective and thresholding.
    // Balance their surrounding patterns against the interior alignment marks,
    // then adjust all eight homography parameters within a small neighborhood.
    // Five scales, at most eight iterations and sixteen neighbors per scale:
    // at most 641 scores, each at most 8445 probes (version 40).
    const double module=(std::hypot(initial.tr.x-initial.tl.x,initial.tr.y-initial.tl.y)
                        +std::hypot(initial.bl.x-initial.tl.x,initial.bl.y-initial.tl.y))/(2*initial.span);
    auto objective=[&](const Map& map){
        const double functional=pattern_score(r,map,n,budget);
        return (functional+alignment_score(r,map,n,budget))*.5;
    };
    Map best=initial;score=objective(best);
    for(double step:{.5,.25,.125,.0625,.03125}) {
        for(int iteration=0;iteration<8&&budget.probes<budget.probe_max;++iteration) {
            Map next=best;double next_score=score;
            for(int axis=0;axis<8&&budget.probes<budget.probe_max;++axis)for(int sign:{-1,1}) {
                if(budget.probes>=budget.probe_max)break;
                Map candidate=best;
                double* parameter[]={&candidate.tl.x,&candidate.tl.y,&candidate.tr.x,&candidate.tr.y,
                                     &candidate.bl.x,&candidate.bl.y,&candidate.g,&candidate.h};
                *parameter[axis]+=sign*step*(axis<6?module:.08);
                if(!admissible(candidate)
                   ||std::hypot(candidate.tl.x-initial.tl.x,candidate.tl.y-initial.tl.y)>.75*module
                   ||std::hypot(candidate.tr.x-initial.tr.x,candidate.tr.y-initial.tr.y)>.75*module
                   ||std::hypot(candidate.bl.x-initial.bl.x,candidate.bl.y-initial.bl.y)>.75*module)continue;
                const double candidate_score=objective(candidate);
                if(candidate_score+1e-9<next_score){next=candidate;next_score=candidate_score;}
            }
            if(next_score>=score)break;
            best=next;score=next_score;
        }
    }
    return best;
}
static bool sample(const Raster& r,const Map& map,int n,std::vector<std::uint8_t>& modules) {
    modules.assign(std::size_t(n)*n,0);
    for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
        bool black=false;if(!read_point(r,map,x+.5,y+.5,black))return false;
        modules[std::size_t(y)*n+x]=black;
    }
    return true;
}
static bool inside_symbol(const Symbol& symbol,const Finder& point) {
    bool positive=false,negative=false;
    for(unsigned i=0;i<4;++i) {
        const Point a=symbol.corners[i],b=symbol.corners[(i+1)%4];
        const double cross=(b.x-a.x)*(point.y-a.y)-(b.y-a.y)*(point.x-a.x);
        positive=positive||cross>0;negative=negative||cross<0;
    }
    return !(positive&&negative);
}
static bool same_position(const Symbol& old,const Map& map,int n) {
    Point center; if(!map.point(n/2.0,n/2.0,center))return false;
    const Point a=old.corners[0],b=old.corners[1],c=old.corners[2],d=old.corners[3];
    const double denominator=(c.x-a.x)*(d.y-b.y)-(c.y-a.y)*(d.x-b.x);
    if(std::abs(denominator)<1e-9)return false;
    const double t=((b.x-a.x)*(d.y-b.y)-(b.y-a.y)*(d.x-b.x))/denominator;
    const Point oldcenter{a.x+t*(c.x-a.x),a.y+t*(c.y-a.y)};
    const double side=std::hypot(old.corners[1].x-old.corners[0].x,old.corners[1].y-old.corners[0].y);
    return std::hypot(oldcenter.x-center.x,oldcenter.y-center.y)<std::max(3.0,side*0.22);
}
static bool attempt(const Raster& r,const Map& map,int n,const Options& options,bool inverted,Budget& budget,ImageResult& result,std::vector<std::uint8_t>& modules) {
    if(budget.attempts>=budget.maximum || budget.probes>=budget.probe_max) {budget.clipped=true;return false;}
    for(const auto& symbol:result.symbols) if(same_position(symbol,map,n))return true;
    ++budget.attempts;budget.probes+=std::uint64_t(n)*n;
    if(!sample(r,map,n,modules))return false;
    Options grid_options=options;grid_options.try_invert=false;
    DecodeResult decoded=decode_grid({modules.data(),std::size_t(n),std::size_t(n),modules.size()},grid_options);
    if(!decoded) {
        if(options.diagnostics&&budget.attempts<=16)result.trace.push_back({"image.grid_reject","dimension="+std::to_string(n)+", g="+std::to_string(map.g)+", h="+std::to_string(map.h)+", error="+error_name(decoded.error)+", "+decoded.message});
        return false;
    }
    Symbol& symbol=decoded.symbol;symbol.inverted=inverted;
    const std::array<Point,4> boundaries={Point{0,0},Point{double(n),0},Point{double(n),double(n)},Point{0,double(n)}};
    for(int i=0;i<4;++i) if(!map.point(boundaries[i].x,boundaries[i].y,symbol.corners[i]))return false;
    if(options.diagnostics) symbol.trace.push_back({"image.sample","dimension="+std::to_string(n)+", projective g="+std::to_string(map.g)+", h="+std::to_string(map.h)});
    result.symbols.push_back(std::move(symbol));return true;
}
static unsigned otsu(const std::array<std::size_t,256>& hist,std::size_t count) {
    long double sum=0;for(unsigned i=0;i<256;++i)sum+=static_cast<long double>(i)*hist[i];
    std::size_t left=0;long double leftsum=0,best=-1;unsigned split=127;
    for(unsigned i=0;i<255;++i) {
        left+=hist[i];leftsum+=static_cast<long double>(i)*hist[i];if(!left||left==count)continue;
        const long double a=leftsum/left,b=(sum-leftsum)/(count-left),between=static_cast<long double>(left)*(count-left)*(a-b)*(a-b);
        if(between>best){best=between;split=i;}
    }
    return split;
}
static Raster threshold(const std::vector<std::uint8_t>& gray,int w,int h,unsigned global,bool local,bool invert) {
    Raster r{w,h,std::vector<std::uint8_t>(gray.size())};
    if(!local){for(std::size_t i=0;i<gray.size();++i)r.pixels[i]=(gray[i]<=global)!=invert;return r;}
    // A summed table of 8x8 tile means costs ~1/4 byte per source pixel.
    // Its support grows with the image, retaining large solid finder centers.
    constexpr int block=8;
    const int tw=(w+block-1)/block,th=(h+block-1)/block;
    std::vector<std::uint64_t> sums(std::size_t(tw+1)*(th+1),0),counts(sums.size(),0);
    for(int ty=0;ty<th;++ty)for(int tx=0;tx<tw;++tx){
        std::uint64_t sum=0,count=0;
        for(int y=ty*block;y<std::min(h,(ty+1)*block);++y)for(int x=tx*block;x<std::min(w,(tx+1)*block);++x){sum+=gray[std::size_t(y)*w+x];++count;}
        const std::size_t p=std::size_t(ty+1)*(tw+1)+tx+1;
        sums[p]=sum+sums[p-1]+sums[p-tw-1]-sums[p-tw-2];counts[p]=count+counts[p-1]+counts[p-tw-1]-counts[p-tw-2];
    }
    const int radius=std::max(3,std::min(w,h)/(16*block));
    for(int ty=0;ty<th;++ty)for(int tx=0;tx<tw;++tx){
        const int x0=std::max(0,tx-radius),x1=std::min(tw,tx+radius+1),y0=std::max(0,ty-radius),y1=std::min(th,ty+radius+1);
        auto rect=[&](const std::vector<std::uint64_t>& v){return v[std::size_t(y1)*(tw+1)+x1]-v[std::size_t(y1)*(tw+1)+x0]-v[std::size_t(y0)*(tw+1)+x1]+v[std::size_t(y0)*(tw+1)+x0];};
        const int mean=int(rect(sums)/rect(counts)),cut=mean-6;
        for(int y=ty*block;y<std::min(h,(ty+1)*block);++y)for(int x=tx*block;x<std::min(w,(tx+1)*block);++x){const auto p=std::size_t(y)*w+x;r.pixels[p]=(int(gray[p])<cut)!=invert;}
    }
    return r;
}
} // namespace

ImageResult read_image(ImageView image,const Options& options) {
    ImageResult result;
    auto fail=[&](Error error,const char* message){result.error=error;result.message=message;if(error==Error::resource_limit)result.work_limit_reached=true;return result;};
    std::size_t row=0,pixels=0,last=0;
    if(!image.data||!image.width||!image.height||(image.channels!=1&&image.channels!=3)
       ||!multiply(image.width,image.channels,row)||image.stride<row
       ||!multiply(image.width,image.height,pixels)||!multiply(image.height-1,image.stride,last)
       ||last>std::numeric_limits<std::size_t>::max()-row||last+row>image.data_size)
        return fail(Error::invalid_input,"invalid image extent, channel count, stride, or buffer size");
    if(image.width>std::min(options.limits.max_dimension,hard_dimension)||image.height>std::min(options.limits.max_dimension,hard_dimension)
       ||pixels>std::min(options.limits.max_pixels,hard_pixels)||!options.limits.max_candidates||!options.limits.max_symbols||!options.limits.max_grid_attempts)
        return fail(Error::resource_limit,"image dimensions, pixel count, or work limits exceeded");
    if(image.width<21||image.height<21)return fail(Error::not_found,"image is smaller than a Model 2 module grid");
    try {
        const int w=int(image.width),h=int(image.height);
        std::vector<std::uint8_t> gray(pixels);std::array<std::size_t,256> histogram{};
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            const auto p=std::size_t(y)*image.stride+std::size_t(x)*image.channels;
            const unsigned value=image.channels==1?image.data[p]:(77u*image.data[p]+150u*image.data[p+1]+29u*image.data[p+2]+128u)/256u;
            gray[std::size_t(y)*w+x]=std::uint8_t(value);++histogram[value];
        }
        const unsigned split=otsu(histogram,pixels);
        Budget budget;budget.maximum=std::min(options.limits.max_grid_attempts,hard_attempts);
        budget.probe_max=std::min<std::uint64_t>(256000000,4000000+std::uint64_t(budget.maximum)*65536);
        const std::size_t symbol_limit=std::min<std::size_t>(options.limits.max_symbols,1024);
        const std::size_t candidate_limit=std::min(options.limits.max_candidates,hard_candidates);
        std::vector<std::uint8_t> modules;
        bool stop=false;
        for(int polarity=0;polarity<(options.try_invert?2:1)&&!stop;++polarity)for(int local=0;local<2&&!stop;++local){
            Raster r=threshold(gray,w,h,split,local!=0,polarity!=0);
            bool candidate_clipped=false;auto found=finders(r,candidate_limit,candidate_clipped);
            auto guesses=hypotheses(found,candidate_clipped);
            if(candidate_clipped)result.work_limit_reached=true;
            if(options.diagnostics)result.trace.push_back({"image.finders",std::string(local?"local":"global")+(polarity?" inverted":"")+": candidates="+std::to_string(found.size())+", triples="+std::to_string(guesses.size())+(candidate_clipped?", candidate cap reached":"")});
            if(options.diagnostics)for(const auto& finder:found)result.trace.push_back({"image.finder","x="+std::to_string(finder.x)+", y="+std::to_string(finder.y)+", module="+std::to_string(finder.module)+", hits="+std::to_string(finder.hits)});
            for(const auto& guess:guesses){
                bool covered=false;
                for(const auto& symbol:result.symbols)if(inside_symbol(symbol,guess.tl)||inside_symbol(symbol,guess.tr)||inside_symbol(symbol,guess.bl)){covered=true;break;}
                if(covered)continue;
                if(budget.attempts>=budget.maximum||budget.probes>=budget.probe_max){budget.clipped=true;stop=true;break;}
                const double dimension=(module_dimension(guess.tl,guess.tr)+module_dimension(guess.tl,guess.bl))/2+7;
                const int version=std::max(1,std::min(40,int(std::round((dimension-17)/4))));
                bool decoded=false;
                for(int delta:{0,-1,1,-2,2,-3,3}){
                    const int v=version+delta;if(v<1||v>40)continue;const int n=17+4*v;
                    Map base{{guess.tl.x,guess.tl.y},{guess.tr.x,guess.tr.y},{guess.bl.x,guess.bl.y},double(n-7),0,0};
                    for(const auto& symbol:result.symbols)if(same_position(symbol,base,n)){decoded=true;break;}
                    if(decoded)break;
                    const double affine_score=pattern_score(r,base,n,budget);
                    if(affine_score<.24&&attempt(r,base,n,options,polarity!=0,budget,result,modules)){decoded=true;break;}
                    Map seeded=base;
                    seeded.g=std::pow(guess.tl.module/guess.tr.module,2.0/3)-1;
                    seeded.h=std::pow(guess.tl.module/guess.bl.module,2.0/3)-1;
                    double projective_score=1;Map improved=refine(r,seeded,n,budget,projective_score);
                    if(projective_score<.22&&attempt(r,improved,n,options,polarity!=0,budget,result,modules)){decoded=true;break;}
                    if(std::abs(seeded.g)+std::abs(seeded.h)>.04){
                        double alternate_score=1;Map alternate=refine(r,base,n,budget,alternate_score);
                        if(alternate_score<.22&&attempt(r,alternate,n,options,polarity!=0,budget,result,modules)){decoded=true;break;}
                        if(alternate_score<projective_score){improved=alternate;projective_score=alternate_score;}
                    }
                    // Spend the extra search only on a promising failed geometry,
                    // once per dimension, and retain the ordinary grid acceptance.
                    if(n>21&&projective_score<.12&&budget.attempts<budget.maximum&&budget.probes<budget.probe_max){
                        double center_score=1;Map centered=refine_centers(r,improved,n,budget,center_score);
                        if(options.diagnostics)result.trace.push_back({"image.refine_centers","dimension="+std::to_string(n)
                            +", balanced pattern score="+std::to_string(center_score)});
                        if(center_score<.12&&attempt(r,centered,n,options,polarity!=0,budget,result,modules)){decoded=true;break;}
                    }
                    if(budget.attempts>=budget.maximum||budget.probes>=budget.probe_max){budget.clipped=true;stop=true;break;}
                }
                if(result.symbols.size()>=symbol_limit){result.work_limit_reached=true;stop=true;break;}
            }
        }
        if(options.diagnostics)result.trace.push_back({"image.work","grid attempts="+std::to_string(budget.attempts)+", sample probes="+std::to_string(budget.probes)});
        result.work_limit_reached=result.work_limit_reached||budget.clipped;
        if(result.symbols.empty())return fail(result.work_limit_reached?Error::resource_limit:Error::not_found,result.work_limit_reached?"image search budget or candidate cap exhausted":"no decodable Model 2 symbol found");
        result.error=Error::none;
        if(budget.clipped)result.message="symbols decoded; image search budget exhausted before completion";
        else if(result.symbols.size()>=symbol_limit)result.message="symbol limit reached; additional symbols may be present";
        else if(result.work_limit_reached)result.message="symbols decoded; finder or hypothesis cap truncated search";
        return result;
    } catch(const std::bad_alloc&) {return fail(Error::resource_limit,"image allocation failed");}
}
} // namespace specqr
