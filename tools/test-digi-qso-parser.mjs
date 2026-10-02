#!/usr/bin/env node

// Exercise the FT8/FT4 QSO-message parser in both shipped frontends.  Extracting
// the actual inline function keeps this test useful while the server and
// standalone pages remain separate build inputs.
import fs from 'node:fs';
import vm from 'node:vm';

const pages = [
    'resources/web/index.html',
    'resources/web-standalone/index.html',
];

const cases = [
    ['standard report', 'W2ERC SQ5ANT -06', 'W2ERC', 'SQ5ANT', ['W2ERC', 'SQ5ANT', '-06']],
    ['standard R-report', 'W2ERC SQ5ANT R-03', 'W2ERC', 'SQ5ANT', ['W2ERC', 'SQ5ANT', 'R-03']],
    ['combined report', 'K0DRE RR73; W2ERC <SQ5ANT> -06', 'W2ERC', 'SQ5ANT', ['W2ERC', 'SQ5ANT', '-06']],
    ['combined completion', 'W2ERC RR73; K0DRE <SQ5ANT> -10', 'W2ERC', 'SQ5ANT', ['W2ERC', 'SQ5ANT', 'RR73']],
    ['manual combined selection', 'K0DRE RR73; W2ERC <SQ5ANT> -06', 'W2ERC', '', ['W2ERC', 'SQ5ANT', '-06']],
    ['unrelated traffic', 'K0DRE SQ5ANT -10', 'W2ERC', 'SQ5ANT', ['K0DRE', 'SQ5ANT', '-10']],
    ['CQ unchanged', 'CQ SQ5ANT KO02', 'W2ERC', '', ['CQ', 'SQ5ANT', 'KO02']],
];

for (const page of pages) {
    const source = fs.readFileSync(page, 'utf8');
    const match = source.match(/function digiQsoMessageParts\([\s\S]*?\n}\n\nfunction digiRowClick/);
    if (!match) throw new Error(`${page}: digiQsoMessageParts not found`);
    const functionSource = match[0].replace(/\n\nfunction digiRowClick$/, '');
    const context = {};
    vm.runInNewContext(functionSource, context, { filename: page });

    for (const [name, message, myCall, partner, expected] of cases) {
        const actual = Array.from(context.digiQsoMessageParts(message, myCall, partner));
        if (JSON.stringify(actual) !== JSON.stringify(expected)) {
            throw new Error(`${page}: ${name}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
        }
    }
    console.log(`${page}: ${cases.length} parser cases passed`);
}
