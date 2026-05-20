Proxy inverso de alto rendimiento basado en el modelo de E/S asíncrona de Linux/Windows.

# Lenguaje C
1. Uso de epoll en linux y IOCP en Windows
2. Redirección por nombre de dominio.
3. Proxy Level 7
4. Podemos tener varias entradas en el proxy listen on diferentes ports
5. Cada entrada tendrá n salidas. La salida será función del dominio.
6. Para cada salida puede haber varios servidores en round robin.

# Gestor del proyecto C
1. Manejar el proyecto con meson.

#Dynamic reload de la configuración.

#Test del funcionamiento del proxy
1. Generar datos de configuración
2. Lanzar servidores de servicio.
3. Dar de alta en Windows\System32\drivers\etc\hosts diferentes dominios.
