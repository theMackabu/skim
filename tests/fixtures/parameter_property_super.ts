class Base {
  constructor(public base: number) {}
}

class Child extends Base {
  constructor(public value: number) {
    super(value + 1);
  }
}

console.log(new Child(11).base);

