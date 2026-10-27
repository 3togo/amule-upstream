# Client identity and status proposal

This is a design prototype for [issue #1666](https://github.com/amule-org/amule/issues/1666), based on upstream revision e6224df4e66a2617dc8ef428da48c43ee8c9fb47. Open `preview.html` in a browser. It does not change the application.

The proposal separates three things currently compressed into a small composite icon: client identity, connection security, and credit preference. Show a dedicated Client column with an icon and software name; show security and credit information as separate text. A successful identity check must not imply that a peer or its files are trustworthy. Unknown status must be shown explicitly, rather than inferred from a missing badge.

The seven SVG illustrations are original drawings made for this proposal, using simple paths and shapes, without tracing or importing third-party logos. They are proposed identity illustrations, not official client logos. They are available under the repository's GPL-2.0-or-later terms. The design uses different silhouettes rather than colour alone: front-facing mule, side-facing mule, walking camel, long-eared donkey, elephant, connected hubs, and crossed ears. Their association with particular applications needs user review; new drawings cannot establish recognizability by themselves.

The preview uses synthetic peers and explicitly illustrative status values. The controls are written to run locally, without network requests. Browser interaction has not been verified. Toggle theme, greyscale, and icon size to assess the shapes. The contact sheet includes actual 16-pixel icons and enlarged nearest-neighbour previews on light and dark backgrounds.

Before an application patch: agree on the identity artwork, verify that each proposed status can be derived accurately from the existing peer model, decide whether a Client column should be optional, and check column width, sorting, translations, accessibility, skins, and the download/upload/queue lists. Preserve custom skins. Do not change status semantics in an artwork patch. wx/NanoSVG rendering and native desktop scaling remain untested by this browser prototype. The issue also covers overlays; this prototype proposes replacing their visual overload with text, but does not implement that change.

Regenerate the PNG fallbacks and contact sheet with `python3 render-preview.py` (Python 3, Pillow, librsvg 2 and Cairo). This small artwork conversion is a local check; no application build or remote test was performed.
