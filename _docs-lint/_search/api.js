/// <reference no-default-lib="true"/>
/// <reference lib="ES2019" />
/// <reference lib="webworker" />

/* eslint-disable new-cap */
/* eslint-env worker */

const DEFAULT_CONFIG = {
    tolerance: 3,
    confidence: 0.5,
    mark: 'search-highlight',
    base: '',
};

const TRIM_WORDS = 30;

const UNKNOWN_HANDLER = {
    message: 'Unknown message type!',
    code: 'UNKNOWN_HANDLER',
};
const NOT_INITIALIZED_CONFIG = {
    message: 'Worker is not initialized with required config!',
    code: 'NOT_INITIALIZED',
};
const NOT_INITIALIZED_API = {
    message: 'Worker is not initialized with required api!',
    code: 'NOT_INITIALIZED',
};

function AssertConfig(config) {
    if (!config) {
        throw NOT_INITIALIZED_CONFIG;
    }
}

function AssertApi(api) {
    if (!api) {
        throw NOT_INITIALIZED_API;
    }
}

self.api = {
    async init() {
        self.config = Object.assign({}, DEFAULT_CONFIG, self.config);
    },
    async suggest(query, count = 10) {
        AssertConfig(self.config);
        const results = await search(self.config, query, count);
        const formattedResults = format(self.config, results);
        return formattedResults;
    },
    async search(query, count = 10, page = 1) {
        AssertConfig(self.config);
        const result = await search(self.config, query, count, page);
        const formattedResults = format(self.config, result);
        return formattedResults;
    },
};

async function search(config, query, count = 10, page = 1) {
    const appId = config.appId;
    const searchApiKey = config.searchApiKey;
    const indexName = config.indexName;
    const querySettings = config.querySettings;

    if (!appId || !searchApiKey || !indexName) {
        throw new Error(
            'Algolia configuration is incomplete. Missing appId, searchApiKey or indexName.',
        );
    }

    const url = `https://${appId}.algolia.net/1/indexes/${indexName}/query`;

    const markClass = config.mark || 'search-highlight';
    const requestBody = Object.assign({}, querySettings, {
        query: query,
        hitsPerPage: count,
        page: page - 1,
        attributesToSnippet: [`content:${TRIM_WORDS}`],
        highlightPreTag: `<span class="${markClass}">`,
        highlightPostTag: `</span>`,
    });

    const response = await fetch(url, {
        method: 'POST',
        headers: {
            'x-algolia-application-id': appId,
            'x-algolia-api-key': searchApiKey,
        },
        body: JSON.stringify(requestBody),
    });

    const data = await response.json();
    return data;
}

function format(config, result) {
    const base = config.base || '';

    if (!result.hits) {
        return [];
    }

    return result.hits.map(function (hit) {
        const url = hit.url;
        const title = hit.title;
        const section = hit.section;
        const anchor = hit.anchor;
        const highlightResult = hit._highlightResult;
        const snippetResult = hit._snippetResult;

        const link = anchor
            ? base.replace(/\/?$/, '') + '/' + url + '#' + anchor
            : base.replace(/\/?$/, '') + '/' + url;

        const highlightedContent =
            highlightResult && highlightResult.content && highlightResult.content.value;
        const description =
            (snippetResult && snippetResult.content && snippetResult.content.value) ||
            (highlightedContent ? trim(highlightedContent, TRIM_WORDS) : '');

        return {
            type: 'page',
            link: link,
            title:
                (highlightResult && highlightResult.title && highlightResult.title.value) || title,
            section: section,
            description: description,
        };
    });
}

function trim(text, words) {
    if (!text) return '';
    const parts = text.split(/\s/);
    if (parts.length > words) {
        return parts.slice(0, words).join(' ') + '...';
    } else {
        return parts.join(' ');
    }
}

const HANDLERS = {
    async init(config) {
        self.config = config;
        if (self.api && self.api.init) {
            return self.api.init();
        }
        return;
    },
    async suggest(data) {
        const {query, count = 10} = data;
        AssertConfig(self.config);
        AssertApi(self.api);
        return self.api.suggest(query, count);
    },
    async search(data) {
        const {query, count = 10, page = 1} = data;
        AssertConfig(self.config);
        AssertApi(self.api);
        return self.api.search(query, count, page);
    },
};

self.onmessage = async function (message) {
    const [port] = message.ports;
    const type = message.data.type;
    const handler = HANDLERS[type];

    if (!handler) {
        port.postMessage({error: UNKNOWN_HANDLER});
        return;
    }

    try {
        const result = await handler(message.data);
        port.postMessage({result});
    } catch (error) {
        port.postMessage({error});
    }
};
