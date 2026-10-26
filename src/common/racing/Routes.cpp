#include "common/racing/Routes.h"
#include <algorithm>
#include <cmath>

namespace coop::street {
float Distance(Vec3 a, Vec3 b) { return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z)); }
float DistanceToSegment(Vec3 p, Vec3 a, Vec3 b) {
    float x=b.x-a.x,y=b.y-a.y,z=b.z-a.z, length=x*x+y*y+z*z;
    float t=length>0 ? std::clamp(((p.x-a.x)*x+(p.y-a.y)*y+(p.z-a.z)*z)/length,0.0f,1.0f) : 0;
    return Distance(p,{a.x+t*x,a.y+t*y,a.z+t*z});
}
float Route::Length() const {
    float result=0;
    for (size_t i=1;i<checkpoints.size();++i) result+=Distance(checkpoints[i-1],checkpoints[i]);
    return result;
}
const std::vector<Route>& Routes() {
    constexpr uint16_t road=ClassBit(VehicleClass::Street)|ClassBit(VehicleClass::Tuned)|ClassBit(VehicleClass::Muscle)|ClassBit(VehicleClass::Sports);
    // Fixed road gates checked against the supported 1.0 US CPRACE road courses.
    // These short sprints stay within one unlocked region; no cross-city shortcuts.
    static const std::vector<Route> routes={
        {1,"Market Sprint",Region::LosSantos,0,1,2,8,road,100,500,
            {{852.054f,-1405.622f,12.217f},{1102.789f,-1405.766f,12.428f},{1353.038f,-1405.544f,12.325f},
             {1600.965f,-1440.852f,12.383f},{1850.622f,-1461.777f,12.398f}}},
        {2,"East Los Santos Run",Region::LosSantos,0,2,2,8,road,150,750,
            {{2875.192f,-1458.850f,10.589f},{2875.362f,-1404.768f,10.714f},{2797.037f,-1291.152f,41.389f},
             {2609.984f,-1256.458f,47.932f},{2371.282f,-1280.477f,23.634f},{2450.557f,-1444.240f,23.632f},
             {2618.436f,-1444.826f,31.623f},{2657.300f,-1404.327f,30.080f}}},
        {3,"Vinewood Climb",Region::LosSantos,0,3,2,8,road,200,1000,
            {{1357.656f,-1361.671f,12.383f},{1357.689f,-1186.404f,20.274f},{1369.741f,-1000.526f,27.020f},
             {1505.818f,-868.781f,61.046f},{1452.794f,-711.532f,89.877f},{1253.572f,-729.259f,93.429f},
             {1059.306f,-775.985f,105.324f},{878.665f,-858.191f,76.883f}}},
        {4,"Red County Run",Region::RedCounty,0,2,2,8,uint16_t(road|ClassBit(VehicleClass::OffRoad)),100,600,
            {{1559.983f,17.468f,26.156f},{1556.426f,-94.035f,19.496f},{1434.738f,-214.964f,7.353f},
             {1321.254f,-195.898f,15.745f},{1211.007f,-102.693f,39.360f},{892.292f,-88.112f,22.441f},
             {789.925f,-124.317f,21.337f}}},
        {5,"San Fierro Streets",Region::SanFierro,1,3,2,8,road,250,1500,
            {{-2728.309f,-310.873f,6.039f},{-2668.671f,-251.621f,5.498f},{-2655.978f,-104.675f,2.998f},
             {-2460.835f,-69.977f,31.200f},{-2421.263f,55.609f,34.016f},{-2545.260f,137.911f,15.218f},
             {-2604.136f,136.858f,3.180f},{-2558.661f,245.625f,10.793f}}},
        {6,"Las Venturas Express",Region::LasVenturas,2,2,2,8,uint16_t(road|ClassBit(VehicleClass::Performance)),500,2500,
            {{1446.426f,834.066f,5.812f},{1747.259f,834.025f,8.682f},{2047.335f,834.192f,5.742f},
             {2348.722f,834.148f,5.742f},{2637.920f,915.628f,5.750f},{2726.824f,1202.266f,5.742f},
             {2726.698f,1502.967f,5.742f},{2726.958f,1804.343f,5.739f}}},
    };
    return routes;
}
const Route* FindRoute(uint16_t id) { for (const auto& r:Routes()) if(r.id==id)return &r; return nullptr; }
VehicleClass ClassOf(uint16_t model) {
    switch(model) {
    case 400:case 424:case 489:case 495:case 500:case 505:return VehicleClass::OffRoad;
    case 411:case 415:case 451:case 541:return VehicleClass::Performance;
    case 429:case 477:case 480:case 506:case 533:case 555:case 587:case 602:case 603:return VehicleClass::Sports;
    case 402:case 412:case 439:case 474:case 475:case 518:case 535:case 536:case 567:case 575:case 576:return VehicleClass::Muscle;
    case 496:case 534:case 558:case 559:case 560:case 561:case 562:case 565:case 589:return VehicleClass::Tuned;
    case 401:case 404:case 405:case 410:case 419:case 421:case 426:case 436:case 445:case 458:case 466:case 467:
    case 492:case 516:case 517:case 526:case 527:case 529:case 540:case 542:case 543:case 545:case 546:case 547:
    case 549:case 550:case 551:case 566:case 580:case 585:case 600:return VehicleClass::Street;
    default:return VehicleClass::None;
    }
}
float MaximumSpeed(VehicleClass c) {
    switch(c) {case VehicleClass::Performance:return 120;case VehicleClass::Sports:return 105;
        case VehicleClass::Tuned:case VehicleClass::Muscle:return 95;case VehicleClass::OffRoad:return 80;
        case VehicleClass::Street:return 75;default:return 0;}
}
bool Eligible(const Route& r,uint16_t model) {return (r.classes&ClassBit(ClassOf(model)))!=0;}
bool Available(const Route& r,bool unlocked,uint8_t city) {return unlocked&&city>=r.cityLevel;}
std::string AllowedClasses(const Route& r){
    constexpr const char* names[]={"","Street","Tuned Street","Muscle","Sports","Off-road","High Performance"};std::string result;
    for(unsigned i=1;i<7;++i)if(r.classes&ClassBit((VehicleClass)i)){if(!result.empty())result+=" / ";result+=names[i];}
    return result;
}
Vec3 GridSpot(const Route& r,unsigned slot) {
    Vec3 a=r.checkpoints.front(),b=r.checkpoints[1];float dx=b.x-a.x,dy=b.y-a.y;
    float length=std::max(1.0f,std::sqrt(dx*dx+dy*dy));dx/=length;dy/=length;
    float back=float(slot/2)*7.0f,side=slot%2 ? 2.2f : -2.2f;
    return {a.x-dx*back+dy*side,a.y-dy*back-dx*side,a.z};
}
}
