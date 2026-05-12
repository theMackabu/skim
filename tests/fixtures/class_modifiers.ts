interface Runnable {
  run(): string;
}

abstract class Base {
  protected readonly count: number = 3;
}

class Job extends Base implements Runnable {
  public run(): string {
    return "ready";
  }

  getCount(): number {
    return this.count;
  }
}

const job = new Job();
console.log(job.run(), job.getCount());

