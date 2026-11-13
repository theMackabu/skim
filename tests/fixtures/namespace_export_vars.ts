export namespace N {
  export const v: number = 1;
  export function f(): number { return v; }
  export let a: string, b = 2, c: Array<number> = [3, 4];
  export const { x, y: z = 6 }: { x: number; y?: number } = { x: 5 };
  export const [p, ...q] = [7, 8];
  export const g = (n: number): number => n + v
  export const h = 10 // trailing
}

namespace One { export const v: number = 11; export function f(): number { return v; } }

console.log(N.v, N.f(), N.a, N.b, N.c.join(), N.x, N.z, N.p, N.q.join(), N.g(8), N.h, One.v, One.f());
