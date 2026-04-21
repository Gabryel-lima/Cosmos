#pragma once
// src/regimes/IRegime.hpp — Interface que todos os regimes cósmicos devem implementar.

#include <string>

struct Universe;
class  Renderer;

class IRegime {
public:
    virtual ~IRegime() = default;

    /// Chamado uma vez quando este regime se torna ativo.
    /// Universe é pré-populado com um InitialState fisicamente consistente.
    virtual void onEnter(Universe& state) = 0;

    /// Chamado uma vez ao sair deste regime (antes do onEnter do próximo).
    virtual void onExit() = 0;

    /// Passo de atualização física.
    /// @param cosmic_dt   delta de tempo cósmico [segundos]
    /// @param scale_factor  a(t), adimensional
    /// @param temp_keV    temperatura atual [keV]
    virtual void update(double cosmic_dt, double scale_factor, double temp_keV,
                        Universe& universe) = 0;

    /// Renderizar o conteúdo deste regime. Chamado por RegimeManager::render().
    virtual void render(Renderer& renderer, const Universe& universe) = 0;

    /// Nome legível para exibição no HUD.
    virtual std::string getName() const = 0;
};
