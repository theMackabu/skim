interface User {
  name: string;
}

type Count = number;

const user: User = { name: "ant" };
let count: Count = 3 as Count;
const doubled = (count satisfies number) * 2;

function label(value: Count): string {
  return user.name + ":" + value;
}

console.log(label(doubled));

