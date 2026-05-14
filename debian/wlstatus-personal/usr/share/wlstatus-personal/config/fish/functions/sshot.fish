function sshedit --description 'Capture area and open in Satty editor'
    grim -g (slurp) -t ppm - | satty --filename -

