KeeperFX — music folder
=======================

This folder holds the game's background music. It ships empty because the
music is the original Dungeon Keeper CD audio, which cannot be redistributed
with the game.

If the game is silent, this folder is almost certainly the reason.


Getting the music
-----------------

Any one of these works:

  * The launcher's "Download music" entry, in the menu next to the Play
    button. This is the easiest option.

  * https://keeperfx.net/workshop/item/393/keeperfx-music

  * Copy it from a digital release of the original game. Steam and GOG both
    keep the tracks as keeper02.ogg .. keeper07.ogg in the game's folder.

  * Rip them from an original CD yourself.

  * Play straight from the CD instead, with the "-cd" command line option or
    the matching option in the launcher.


File names
----------

You do not have to rename anything. Files named keeper02.ogg .. keeper07.ogg
are used exactly as the original game used them, and anything else is matched
by the number in its name, so all of these work:

    keeper02.ogg      Track 02.flac      02.wav

If your files carry no numbers at all, they are used in alphabetical order
instead, starting from the land view.

Track 2 plays on the land view, tracks 3 to 6 cycle during play, and track 7
is used by some campaigns.


File formats
------------

OGG, FLAC, WAV and MP3 all play. No conversion step is needed.

Which to use:

  * OGG is the right default. The music pack is already OGG, and it was made
    from the CD, so converting it to FLAC only makes the files several times
    larger without recovering anything.

  * FLAC is worth it only if you rip the original CD yourself, where it does
    preserve the disc exactly. Expect roughly four to five times the size.

  * WAV has no advantage over FLAC — same quality, much larger.

  * MP3 is best avoided for these tracks. Each one loops continuously, and
    MP3 encoding adds a short silence at the start and end of the file, so
    you hear a small gap every time a track repeats. OGG, FLAC and WAV loop
    seamlessly.

Where a track exists in more than one format, keeperNN.ogg is preferred, so
adding a FLAC copy alongside will not change what you already hear. Delete
the OGG if you want the FLAC used instead.


If something is not playing
---------------------------

Open keeperfx.log in the game folder and look for lines beginning "Sync:"
that mention the music index. The game reports how many tracks it found, and
names any file it did not use along with the reason — a missing track number,
a better-quality copy of the same track winning, or more files present than
there are tracks to fill.
