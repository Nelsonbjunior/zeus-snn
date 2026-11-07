idf_component_register(SRCS "main.c"
                            "zeus_snn.c"
                            "zeus_cerebro.c"
                            "zeus_face.c"
                            "oled_port.c"
                            "screens/i2c_drv.c"
                            "screens/ssd1306_generic.c"
                            "screens/ssd1306_fonts_generic.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "web/index.html")

# a rede neural roda 1000 passos por segundo: so ela compila otimizada (o resto segue -Og)
set_source_files_properties(zeus_snn.c PROPERTIES COMPILE_OPTIONS "-O2")
