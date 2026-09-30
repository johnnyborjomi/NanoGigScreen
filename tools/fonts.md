# Generated fonts

`components/nano_ui/fonts/montserrat_medium_{10,12}.c` are Montserrat Medium (weight 500) and
`montserrat_bold_10.c` is Montserrat Bold (the FX category tags), from
[JulietaUla/Montserrat](https://github.com/JulietaUla/Montserrat) (SIL Open Font License),
converted with lv_font_conv for the FX tiles and the gate button:

```sh
curl -sL -o Montserrat-Medium.ttf https://github.com/JulietaUla/Montserrat/raw/master/fonts/ttf/Montserrat-Medium.ttf
for sz in 10 12; do
  npx --yes lv_font_conv --font Montserrat-Medium.ttf --size $sz --bpp 4 --format lvgl \
    --range 0x20-0x7F --lv-include lvgl.h --no-compress -o montserrat_medium_$sz.c
done
curl -sL -o Montserrat-Bold.ttf https://github.com/JulietaUla/Montserrat/raw/master/fonts/ttf/Montserrat-Bold.ttf
npx --yes lv_font_conv --font Montserrat-Bold.ttf --size 10 --bpp 4 --format lvgl \
  --range 0x20-0x7F --lv-include lvgl.h --no-compress -o montserrat_bold_10.c
```

The built-in LVGL Montserrat fonts (regular weight) are used everywhere else.
