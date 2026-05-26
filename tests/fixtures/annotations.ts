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

class Timeline {
  async timeline(
    limit: number = 10,
    cursor?: string
  ): Promise<string[]> {
    return [String(limit), cursor ?? "none"];
  }
}

const timeline = new Timeline();
const posts = await timeline.timeline();

console.log(label(doubled), posts.join(","));
