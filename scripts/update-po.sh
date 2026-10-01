#! /bin/bash

die () {
	local ERR_CODE=$?
	[[ $ERR_CODE == 0 ]] && return

	local EXIT_CODE=$1
	shift
	local MESSAGE=( "$@" )

	[[ ${MESSAGE[*]} != "" ]] && echo "${MESSAGE[@]}" >&2
	exit "${EXIT_CODE}"
}

usage() {
	echo "Regenerates po/amule.pot from source and merges it into all po/*.po files"
	echo
	echo "Usage: $0 [-h | --help]"
	echo "  -h"
	echo "  --help       Display this help message"
}

if ! PARAMS=$(getopt -o "h" -l "help" -n "$0" -- "$@"); then
	usage
	false; die 10
fi

eval set -- "$PARAMS"

while true; do
	case "$1" in
	-h | --help )
		usage
		false; die 0
		;;
	-- )
		shift
		break
		;;
	* )
		usage
		false; die 12 "Error processing command line parameters"
		;;
	esac
done

GIT_ROOT=$(git rev-parse --show-toplevel)
[[ ${PWD} == "${GIT_ROOT}" ]]
die 12 \
	" The current path is: '${PWD}'" \
	$'\n' \
	"This script must be run in '${GIT_ROOT}'"

if ! command -v xgettext &>/dev/null; then
	echo "Error: xgettext not found. Install gettext." >&2
	exit 20
fi

if ! command -v msgmerge &>/dev/null; then
	echo "Error: msgmerge not found. Install gettext." >&2
	exit 21
fi

if ! command -v msgcat &>/dev/null; then
	echo "Error: msgcat not found. Install gettext." >&2
	exit 22
fi

# Generate separately so extraction failures leave the committed template intact.
POT_WORK=$(mktemp -d "${GIT_ROOT}/po/.update-po.XXXXXX")
die 23 "failed to create temporary catalog directory"
trap 'rm -rf "${POT_WORK}"' EXIT
NEW_POT="${POT_WORK}/amule.pot"

echo "Extracting translatable strings into po/amule.pot ..."
# --no-wrap: project policy is one msgid/msgstr per line, no width-based
# wrapping. Keeps Weblate / msgmerge / hand-regen diffs to real content
# changes instead of reflow noise. Weblate's "application" component is
# configured to match.
#
# --add-location=file: emit "#: src/amuleDlg.cpp" instead of
# "#: src/amuleDlg.cpp:1234". A line number shifts whenever anything above a
# string is edited, so with full locations every catalog rewrote its entire
# reference block on every regen -- the overwhelming bulk of a catalog diff
# was renumbering rather than content, and any two PRs in flight conflicted
# on it. File names rarely move, so translators keep the "where is this
# string used" context Weblate shows while the churn goes away. msgmerge
# below needs the same flag, otherwise it re-expands locations to full
# file:line when merging the pot into each .po.
xgettext \
	--no-wrap \
	--add-location=file \
	--keyword=_ \
	--keyword=wxTRANSLATE \
	--keyword=wxPLURAL:1,2 \
	--files-from=po/POTFILES.in \
	--output="${NEW_POT}" \
	--from-code=UTF-8 \
	--add-comments=TRANSLATORS \
	--copyright-holder='Free Software Foundation, Inc.' \
	--package-name='aMule' \
	--package-version='GIT' \
	--msgid-bugs-address='https://github.com/amule-org/amule/issues'
die 30 "xgettext failed"

# xgettext writes "Copyright (C) YEAR" as a placeholder; fill it in.
YEAR=$(date +%Y)
sed -e "1,5 s/^# Copyright (C) YEAR /# Copyright (C) ${YEAR} /" "${NEW_POT}" > "${NEW_POT}.tmp" \
	&& mv "${NEW_POT}.tmp" "${NEW_POT}"
die 32 "failed to substitute copyright year in regenerated template"

# xgettext stamps "charset=CHARSET" whenever every extracted msgid is ASCII
# (it only infers UTF-8 once it sees a non-ASCII byte). An all-ASCII template
# is legitimate, but "CHARSET" is not a portable encoding name -- msgcat and
# the catalog-sync check reject it. Pin the header to UTF-8, which is correct
# for ASCII and already matches every .po.
awk '
	/^msgid / { header = ($0 == "msgid \"\""); translation = 0 }
	/^msgstr / { translation = 1 }
	/^"/ && !translation { header = 0 }
	header && translation && /^"Content-Type: / {
		sub(/charset=CHARSET/, "charset=UTF-8")
	}
	{ print }
' "${NEW_POT}" > "${NEW_POT}.tmp" \
	&& mv "${NEW_POT}.tmp" "${NEW_POT}"
die 33 "failed to normalise charset in regenerated template"

# Ignore extraction time, copyright year, references and entry ordering when
# comparing templates. Keep translator comments and format flags: changes to
# these matter even when the message text stays the same. Canonicalize with
# gettext rather than comparing msgid lines (which misses contexts and plurals).
canonical_content() {
	local INPUT=$1
	local OUTPUT=$2
	msgcat --no-wrap --no-location --sort-output "${INPUT}" \
		--output-file="${OUTPUT}.canonical" || return
	awk '
		/^msgid / { header = ($0 == "msgid \"\""); translation = 0 }
		/^msgstr / { translation = 1 }
		/^"/ && !translation { header = 0 }
		header && translation && /^"POT-Creation-Date: / { next }
		/^# Copyright \(C\) [0-9][0-9][0-9][0-9] / { next }
		{ print }
	' "${OUTPUT}.canonical" > "${OUTPUT}"
}

canonical_content "${NEW_POT}" "${POT_WORK}/new.content"
die 34 "failed to normalize regenerated template"

# A malformed committed template must not prevent regeneration from repairing it.
if [[ -f po/amule.pot ]] &&
	canonical_content po/amule.pot "${POT_WORK}/old.content"; then
	if cmp -s "${POT_WORK}/old.content" "${POT_WORK}/new.content"; then
		# Read the normalized header so wrapped dates retain their full value.
		# awk keeps the literal \n escaped.
		awk '
			/^msgid / { header = ($0 == "msgid \"\""); translation = 0 }
			/^msgstr / { translation = 1 }
			/^"/ && !translation { header = 0 }
			FNR == NR {
				if (header && translation && /^"POT-Creation-Date: /) previous_date = $0
				next
			}
			header && translation && /^"POT-Creation-Date: / && previous_date != "" {
				$0 = previous_date
			}
			{ print }
		' "${POT_WORK}/old.content.canonical" "${NEW_POT}" > "${NEW_POT}.tmp"
		die 35 "failed to preserve template creation date"
		mv "${NEW_POT}.tmp" "${NEW_POT}"
		die 35 "failed to install preserved template creation date"
	fi
fi
mv "${NEW_POT}" po/amule.pot
die 36 "failed to install regenerated template"

echo "Merging po/amule.pot into each .po file ..."
# Write via --output-file + mv rather than --update: msgmerge --update
# treats the .po as up-to-date when only the pot's wrap differs from
# the .po, and silently skips rewriting -- which defeats --no-wrap on
# any .po that was previously committed in wrapped form.
for PO_FILE in po/*.po; do
	echo "  $PO_FILE"
	msgmerge --no-wrap --add-location=file "$PO_FILE" po/amule.pot --output-file="$PO_FILE.tmp"
	die 31 "msgmerge failed for $PO_FILE"
	mv "$PO_FILE.tmp" "$PO_FILE"
	die 31 "failed to install merged $PO_FILE"
done

echo "Done."
exit 0
