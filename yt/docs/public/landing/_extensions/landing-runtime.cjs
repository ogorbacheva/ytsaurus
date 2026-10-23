'use strict';

const fs = require('node:fs');
const {createRequire} = require('node:module');
const path = require('node:path');

// Resolve next to the CLI for both local and global npm installations.
const cliRequire = createRequire(require.main?.filename || __filename);
const {getHooks} = cliRequire('@diplodoc/cli/lib/program');

class Extension {
    apply(program) {
        // CLI 5.56.0 embeds a Page Constructor runtime incompatible with its
        // current React client. Emit the compatible, explicitly pinned runtime.
        getHooks(program).AfterAnyRun.tapPromise(
            {name: 'YTsaurusLandingRuntime', before: 'BuildContentMap'},
            async (run) => {
                // md2md does not discover font URLs inside custom CSS. Ship the
                // same font files and license in both publication formats.
                await run.copy(
                    path.join(run.input, 'ru/_assets/fonts'),
                    path.join(run.output, 'ru/_assets/fonts'),
                );
                for (const [entry, filename] of [
                    ['runtime', 'page-constructor-extension.js'],
                    ['styles', 'page-constructor-extension.css'],
                ]) {
                    const content = fs.readFileSync(
                        cliRequire.resolve(`@diplodoc/page-constructor-extension/${entry}`),
                        'utf8',
                    );
                    await run.write(path.join(run.output, '_bundle', filename), content, true);
                }
            },
        );
    }
}

module.exports = {Extension};
