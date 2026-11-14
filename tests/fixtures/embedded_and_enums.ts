let n = 0;
for (let i = 0; i < 3; i++) type Skip = number;
n++;
if (n > 5) interface Gone {}
else n += 10;

enum Bits { A = 1, B = A << 2, C = B >> 1, D = 1 << 4 | A }
enum Fwd { X = Y, Y = 2 }

const make = <T>(): (() => T) => (() => 7) as any;
type Deep<T> = T extends infer R extends string ? R : never;

let z = 1;
const o = { z: z };
({ z: z } = { z: 9 });

interface OnlyType {}
export default OnlyType;

console.log(n, Bits.B, Bits.C, Bits.D, Fwd.X, Fwd.Y, make<number>()(), o.z, z);

const wrap = () => <{ k: number }>{ k: 3 };
const text = "a\
  b";
class Base {}
class Derived extends Base { constructor(public w: number) { super() } }
enum Quoted { 'q"r' = 5 }
const parts = { n: n as number, t: `x${n}y`, last: [1, 2].pop()! };
const pick = n ? (z): number => (z) : null;
declare const never: number;
(function () { n++; })()
namespace Tail { export const sum = <T extends number>(v: T) => v + 1; }
console.log(wrap().k, text.length, new Derived(4).w, Quoted['q"r'], parts.n, parts.t, parts.last, typeof pick, n, Tail.sum(1));

class Fields {
  set: Set<number>
  size: number = 2
  ratio() { return (this.size! / 4) + (n < 0 || n >= 12 ? 1 : 0); }
}
console.log(new Fields().ratio(), Object.keys(new Fields()).join());

const nul = "a\x003";
console.log(nul.length, nul.charCodeAt(2));
