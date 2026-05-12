import { RuntimeThing, MissingType, type ExplicitType } from "./mixed_mod.mjs";

const one = null as unknown as MissingType;
const two: ExplicitType | null = null;
void one;
void two;
console.log(RuntimeThing);

