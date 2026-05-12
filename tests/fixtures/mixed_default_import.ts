import DefaultThing, { RuntimeThing, MissingType } from "./mixed_default_mod.mjs";

const typeOnly = null as unknown as MissingType;
void typeOnly;
console.log(DefaultThing + RuntimeThing);

