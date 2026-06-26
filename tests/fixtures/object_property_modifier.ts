function uploadSnippetFile(file: string, opts: { private: boolean }): string {
  return `${file}:${opts.private}`;
}

const isPrivate = true;
const config = { private: isPrivate, public: false, readonly: 1 };
const { private: priv = false } = config;

console.log(uploadSnippetFile("a.ts", { private: isPrivate }), config.public, config.readonly, priv);
