# RPGMakerScraper
> a tool that parses and scrapes (at the time of development) 'RPG Maker MV' json data and finds references to variables, switches and more written in c++17

## usage

* either download the .exe or compile the source code by making your own visual studio project and adding the repo files
* drop the executable inside a directory where your RPG Maker MV project rests.
> there should be a `data/` folder in the root directory

list references to variable id '143'
> `RPGMakerScraper -v 143`

list references to variable id '24' and store the results in 'var_24.txt'
> `RPGMakerScraper -v 24 var_24.txt`

it's that easy.

## notes

this was a quick and dirty side-project that piqued my interest. 
so there's still a lot of stuff that needs to be done like:

* adding RPGMaker 'switch' support
* caching results so all the events don't need to be scraped anymore
* fixing output to support wchar_t/unicode
* finding results in a more optimized manner (maybe multithreading the parsing/scraping?)
* allowing multiple queries and multiple uses

but it gets the job done and i'll probably get around to them eventually - no promises.

## license stuff

uses [nlohmann/json](https://github.com/nlohmann/json) for json