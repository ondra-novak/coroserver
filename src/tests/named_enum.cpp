#include <coroserver/static_lookup.h>
#include <iostream>

NAMED_ENUM(Colors,
        red,green,blue,black,yellow,magenta,white,cyan
);

constexpr NamedEnum_Colors strColors;

constexpr auto strColorsMan =  coroserver::makeStaticLookupTable<Colors,std::string_view>({
    {Colors::red, "red"},
    {Colors::green, "green"},
    {Colors::green, "green1"},
    {Colors::green, "green2"},
    {Colors::green, "green3"},
    {Colors::green, "green4"},
    {Colors::green, "green5"},
    {Colors::green, "green6"},
    {Colors::green, "green7"},
    {Colors::green, "green8"},
    {Colors::green, "green9"},
    {Colors::blue, "blue"},
    {Colors::yellow, "yellow"},
    {Colors::yellow, "yellow1"},
    {Colors::yellow, "yellow2"},
    {Colors::yellow, "yellow3"},
    {Colors::yellow, "yellow4"},
    {Colors::yellow, "yellow5"},
    {Colors::cyan, "cyan"},
    {Colors::cyan, "cyan1"},
    {Colors::cyan, "cyan2"},
});


int main() {

    std::cout << strColors[Colors::magenta] << std::endl;
    std::cout << strColors[Colors::cyan] << std::endl;
    std::cout << strColors[Colors::red] << std::endl;
    std::cout << strColors[Colors::green] << std::endl;
    std::cout << "--------------" << std::endl;
    std::cout << strColorsMan[Colors::magenta] << std::endl;
    std::cout << strColorsMan[Colors::blue] << std::endl;
    std::cout << strColorsMan[Colors::red] << std::endl;
    std::cout << strColorsMan[Colors::green] << std::endl;
    std::cout << strColorsMan[Colors::cyan] << std::endl;


}
