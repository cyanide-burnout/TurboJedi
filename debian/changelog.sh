#!/bin/sh

# TurboJedi, Turbo Journald Interface
# https://github.com/cyanide-burnout/TurboJedi
# Artem Prilutskiy, 2026

# debian/changelog from git: an entry per tag with the commits it has brought, the newest on top,
# the commits after the last tag go to the version being built; without git a single entry of the build

PACKAGE=$1
RELEASE=$2
AUTHOR=$3

# Version, range of commits, date
entry()
{
  printf '%s (%s) unstable; urgency=low\n\n' "${PACKAGE}" "$1"
  git log --no-merges --format='  * %s' $2 | grep . || printf '  * Build of %s\n' "$1"
  printf '\n -- %s  %s\n\n' "${AUTHOR}" "$3"
}

if [ ! -e .git ]
then
  # An archive of git or a copy
  printf '%s (%s) unstable; urgency=low\n\n  * Build of %s\n\n -- %s  %s\n' "${PACKAGE}" "${RELEASE}" "${RELEASE}" "${AUTHOR}" "$(date -R)"
  exit 0
fi

TAGS=$(git tag --list 'v*' --merged HEAD --sort=-v:refname)
LAST=$(echo "${TAGS}" | head -n 1)

if [ -z "${LAST}" ]
then
  # No release yet, all the history goes to this one
  entry "${RELEASE}" HEAD "$(date -R)"
  exit 0
fi

if [ "${RELEASE}" != "${LAST#v}" ]
then
  # Commits after the last tag or changes not committed yet
  entry "${RELEASE}" "${LAST}..HEAD" "$(date -R)"
fi

for TAG in ${TAGS}
do
  PREVIOUS=$(echo "${TAGS}" | sed -n "/^${TAG}\$/{n;p;}")
  entry "${TAG#v}" "${PREVIOUS:+${PREVIOUS}..}${TAG}" "$(git log -1 --format=%aD "${TAG}")"
done
