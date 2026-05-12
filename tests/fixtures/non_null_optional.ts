type Maybe = {
  nested?: {
    value?: number;
  };
};

const maybe: Maybe = { nested: { value: 5 } };
function use(value?: Maybe): string {
  return value!.nested!.value ? "yes" : "no";
}

console.log(use(maybe), maybe.nested!.value);
