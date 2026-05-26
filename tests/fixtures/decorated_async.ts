function dec(..._args: any[]) {}

class Worker {
  @dec
  async run(
    value: number
  ): Promise<number> {
    return value;
  }
}
