foreach(size 64)
    install(DIRECTORY ${size}x${size} DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor
             PATTERN .* EXCLUDE
             PATTERN *~ EXCLUDE)
endforeach(size 64)
