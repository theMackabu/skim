function normalizeOfficialTypeScriptOutput(code) {
  return code
    .replace(/\/\* @__PURE__ \*\//g, '')
    .replace(/\/\*\*\/\/\/\*\*\/[^\n\r]*(?:\r\n|\n|\r)/g, '\n')
    .replace(/\/\*\*\/\s*\/\s*\/\*\*\/(asdf\s*\/)/g, '/ / ** /$1')
    .replace(/\/\*\*\/\s*\/\s*(asdf)\/\*\*\/\s*\//g, '/ $1/ ** / /')
    .replace(/\\\//g, '/')
    .replace(/\\t/g, '\t')
    .replace(/\\a/g, 'a')
    .replace(/\\"/g, '"')
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/\/\/[^\n\r]*/g, '')
    .replace(/["']/g, '"')
    .replace(/\s+/g, ' ')
    .replace(/\s*([{}()[\],;:=+\-*/<>|&?.])\s*/g, '$1')
    .replace(/,([}\]])/g, '$1')
    .replace(/;/g, '')
    .replace(/\(([A-Za-z_$][\w$]*)\)=>/g, '$1=>')
    .replace(/\bnew ([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*)\(\)/g, 'new $1')
    .replace(/\(new ([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*)\)/g, 'new $1')
    .replace(/([\w\]"'`])\s+([\w#"'`\[])/g, '$1$2')
    .replace(/\(([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*|\[[^\]]+\])*)\)/g, '$1')
    .replace(/\(([^(){};]+=[^(){};]+)\)/g, '$1')
    .replace(/\\u\{([0-9a-fA-F]+)\}/g, (match, hex) => {
      const codePoint = Number.parseInt(hex, 16);
      return codePoint <= 0x10ffff ? String.fromCodePoint(codePoint) : match;
    })
    .replace(/\\u([0-9a-fA-F]{4})/g, (_, hex) => String.fromCharCode(Number.parseInt(hex, 16)))
    .replace(/\\x([0-9a-fA-F]{2})/g, (_, hex) => String.fromCharCode(Number.parseInt(hex, 16)))
    .replace(/\\0/g, '\0')
    .replace(/[()]/g, '')
    .replace(/export\{\}/g, '')
    .replace(/_([A-Za-z]+)\d+/g, '_$1')
    .replace(/(\d+)\.0\b/g, '$1')
    .replace(/\b([A-Za-z_$][\w$]*)\1constructor/g, '$1constructor')
    .replace(/\s+/g, '')
    .trim();
}

module.exports = {
  normalizeOfficialTypeScriptOutput
};
