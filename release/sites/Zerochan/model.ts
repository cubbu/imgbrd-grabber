function completeImage(img: IImage): IImage {
    if (!img["file_url"] || img["file_url"].length < 5) {
        img["file_url"] = img["preview_url"]
            .replace(".240.", ".full.")
            .replace(".600.", ".full.")
            .replace("/240/", "/full/")
            .replace("/600/", "/full/");
    }

    img["file_url"] = img["file_url"].replace(/\/s\d+\.zerochan/, "/static.zerochan");

    return img;
}

export const source: ISource = {
    name: "Zerochan",
    modifiers: [],
    forcedTokens: ["filename", "date"],
    tagFormat: {
        case: "upper",
        wordSeparator: " ",
    },
    auth: {},
    apis: {
        rss: {
            name: "RSS",
            auth: [],
            forcedLimit: 100,
            search: {
                url: (query: any, opts: any, previous: any): string | IError => {
                    try {
                        const pagePart = Grabber.pageUrl(query.page, previous, 100, "p={page}", "o={max}", "o={min}");
                        return "/" + query.search + "?s=id&xml&" + pagePart;
                    } catch (e) {
                        return { error: e.message };
                    }
                },
                parse: (src: string): IParsedSearch => {
                    const parsed = Grabber.parseXML(src);
                    const data = Grabber.makeArray(parsed.rss.channel.item);

                    const images: IImage[] = [];
                    for (const image of data) {
                        const img: IImage = {
                            page_url: image["link"]["#text"],
                            tags: image["media:keywords"]["#text"].trim().split(", "),
                            preview_url: image["media:thumbnail"]["#text"] || image["media:thumbnail"]["@attributes"]["url"],
                            file_url: image["media:content"]["#text"] || image["media:content"]["@attributes"]["url"],
                        };
                        img["id"] = Grabber.regexToConst("id", "/(?<id>\\d+)", img["page_url"]);
                        images.push(completeImage(img));
                    }

                    return {
                        images,
                        imageCount: Grabber.countToInt(Grabber.regexToConst("count", "has (?<count>[0-9,]+) .+? anime images", parsed.rss.channel.description["#text"])),
                    };
                },
            },
        },
        html: {
            name: "Regex",
            auth: [],
            forcedLimit: 22,
            search: {
                url: (query: any, opts: any, previous: any): string | IError => {
                    try {
                        const pagePart = Grabber.pageUrl(query.page, previous, 100, "p={page}", "o={max}", "o={min}");
                        return "/" + query.search + "?" + pagePart;
                    } catch (e) {
                        return { error: e.message };
                    }
                },
                parse: (src: string): IParsedSearch => {
                    return {
                        tags: Grabber.regexToTags("<li[^>]*>\\s*<a [^>]+>(?<name>[^>]+)</a>\\s+(?:<span>(?<type>[^<]+) (?<count>[0-9]+)</span>|(?<type_2>[^<]*))\\s*</li>", src),
                        images: Grabber.regexToImages("<a href=['\"]/(?<id>[^'\"]+)['\"][^>]*>[^<]*(?:<b>[^<]*</b>)?[^<]*(?:<span>[^<]*</span>)?[^<]*(?<image><img\\s*src=['\"](?<preview_url>[^'\"]*)['\"][^>]*/?>)", src).map(completeImage),
                        pageCount: Grabber.countToInt(Grabber.regexToConst("page", "page (?:[0-9,]+) of (?<page>[0-9,]+)", src)),
                        imageCount: Grabber.countToInt(Grabber.regexToConst("count", "has (?<count>[0-9,]+) .+? anime images", src)),
                    };
                },
            },
            details: {
                url: (id: number, md5: string): string => {
                    return "/" + id;
                },
                parse: (src: string): IParsedDetails => {
                    return {
                        tags: Grabber.regexToTags("<li[^>]*>\\s*<a [^>]+>(?<name>[^>]+)</a>\\s+(?:<span>(?<type>[^<]+) (?<count>[0-9]+)</span>|(?<type_2>[^<]*))\\s*</li>", src),
                        imageUrl: Grabber.regexToConst("url", '<div id="large">\\s*<a href="(?<url>[^"]+)"[^>]* tabindex="1">', src),
                        createdAt: Grabber.regexToConst("date", 'Entry by <a href="[^"]+">[^<]+</a>\\s*<span title="(?<date>[^"]+)">', src),
                    };
                },
            },
            check: {
                url: (): string => {
                    return "/";
                },
                parse: (src: string): boolean => {
                    return src.search(/© [0-9]+-[0-9]+ Zerochan/) !== -1;
                },
            },
        },
    },
};
