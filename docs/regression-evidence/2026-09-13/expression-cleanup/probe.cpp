#include "recipe/Expression.h"
#include <algorithm>
#include <cstdio>
using namespace BetterEnchantmentEffects;
int main() {
  for (const auto text : {"if(1, 2, [3, 4])", "if(0, 2, [3, 4])", "if(1, 2, [3, 4]) + [1, 2, 3]"}) {
    auto p = Program::Parse(text).value();
    auto t = p.Check({});
    auto v = p.Evaluate({});
    std::printf("%s: check=%s runtime=%s scalar=%g\n", text, t ? Name(*t) : t.error().c_str(), Name(TypeOf(v)), AsScalar(v));
  }
  std::string text = "1+2*3";
  for(int i=0;i<31;++i) text="lerp(1,2,"+text+")";
  auto p = Program::Parse(text);
  if(!p) { std::printf("parse error: %s\n",p.error().c_str()); return 1; }
  int live=0, peak=0;
  for(auto node:p->Code()) {
    if(node.op==Program::Op::kNumber) ++live;
    else if(node.op==Program::Op::kLerp) live-=2;
    else --live;
    peak=std::max(peak,live);
  }
  std::printf("31 nested lerp(1,2,E), E=1+2*3: ops=%zu peak=%d check=%s result=%g expected=38\n",p->OpCount(),peak,Name(p->Check({}).value()),AsScalar(p->Evaluate({})));
}
