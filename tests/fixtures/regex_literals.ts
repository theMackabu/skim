const regexSource = /\{&([\s\S]*?)&\}/gms;

function run(input: string) {
  const regex = new RegExp(regexSource.source, regexSource.flags);
  const results: string[] = [];
  let m: RegExpExecArray | null;
  while ((m = regex.exec(input)) !== null) {
    if (m.index === regex.lastIndex) regex.lastIndex++;
    results.push(m[1]);
  }
  return results;
}

class Wrapper {
  async runSync(input: string) {
    const regex = /\{&([\s\S]*?)&\}/gms;
    const results: string[] = [];
    let m: RegExpExecArray | null;
    while ((m = regex.exec(input)) !== null) {
      if (m.index === regex.lastIndex) regex.lastIndex++;
      results.push(m[1]);
    }
    return results;
  }

  async runAsyncAwait(input: string) {
    await Promise.resolve();
    const regex = /\{&([\s\S]*?)&\}/gms;
    const results: string[] = [];
    let m: RegExpExecArray | null;
    while ((m = regex.exec(input)) !== null) {
      if (m.index === regex.lastIndex) regex.lastIndex++;
      results.push(m[1]);
    }
    return results;
  }
}

const w = new Wrapper();
for (const input of ['{&1&} and {&2&}', '{& 1+1 &} and {& 3*3 &}']) {
  console.log(JSON.stringify(run(input)));
  console.log(JSON.stringify(await w.runSync(input)));
  console.log(JSON.stringify(await w.runAsyncAwait(input)));
}
