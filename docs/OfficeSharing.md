# Excluding office documents from sharing

The Directories preference **Exclude office documents from sharing** prevents
Word, Excel, PowerPoint, OpenDocument, RTF, CSV and TSV files from being added to
the upload list. Extension matching ignores case and includes macro-enabled
variants. This is an extension policy, not content inspection.

The default is enabled for both new profiles and existing profiles that have no
saved value. It applies to native packages as well as Flatpak. Users who want to
share these files can disable the preference; an explicit saved choice survives
restart. Downloads continue and files remain on disk. Partial/completed downloads
and renamed partial files obey the same sharing filter. Custom wildcard and
regular-expression exclusions still apply independently.

The remote GUI shows the control only when the core advertises the preference;
the EC tag and web API schema carry explicit false values as well as true values.
This change is independent of the Flatpak folder permission and kernel policy.

The integration test first shares office files with the preference disabled,
then restarts that same profile with the key absent to exercise the upgrade
default against its existing known-file database. A final restart verifies the
explicit opt-out and that no files were removed. Classification and EC/API
schema tests cover the remaining deterministic rules.
