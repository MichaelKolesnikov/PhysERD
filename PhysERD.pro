TEMPLATE = subdirs

SUBDIRS = Core Main App Test

Main.depends = Core
App.depends = Main
Test.depends = Core

OTHER_FILES += .gitignore README.md LICENSE
