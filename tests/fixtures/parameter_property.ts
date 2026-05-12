class Box {
  constructor(public value: number, private label = "n") {}
}

const box = new Box(9);
console.log(box.value);

