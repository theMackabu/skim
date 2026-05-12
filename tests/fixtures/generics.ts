function id<T>(value: T): T {
  return value;
}

class Holder<T> {
  value: T;
  constructor(value: T) {
    this.value = value;
  }
}

console.log(id<number>(4), new Holder<string>("ok").value);

