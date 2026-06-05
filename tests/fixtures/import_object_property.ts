import { x, y, unusedValue, MissingType, type Shape } from './object_property_mod.mjs';

const nested = {
  outer: {
    x,
    value: y,
  },
};

const typed: { value: Shape } = {
  value: y,
};

const typeOnly = null as unknown as MissingType;
const selected = typed.value > 0 ? x : y;
void typeOnly;

console.log(nested.outer.x + nested.outer.value + typed.value + selected);
