local RotateAndTint = {}

RotateAndTint.Properties = {
    Speed = { type = "float", default = 90.0, min = 0.0, max = 720.0, tooltip = "Degrees per second" },
    ColorSpeed = { type = "float", default = 0.25, min = 0.01, max = 4.0, tooltip = "Hue cycle speed" }
}

local function HSVToRGB(h, s, v)
    local i = math.floor(h * 6.0)
    local f = h * 6.0 - i
    local p = v * (1.0 - s)
    local q = v * (1.0 - f * s)
    local t = v * (1.0 - (1.0 - f) * s)
    i = i % 6

    if i == 0 then
        return v, t, p
    elseif i == 1 then
        return q, v, p
    elseif i == 2 then
        return p, v, t
    elseif i == 3 then
        return p, q, v
    elseif i == 4 then
        return t, p, v
    end

    return v, p, q
end

function RotateAndTint:OnUpdate(dt)
    local rotation = Luma.Transform.GetRotation(self.entity)
    rotation.y = rotation.y + (self.Properties.Speed * dt)
    Luma.Transform.SetRotation(self.entity, rotation.x, rotation.y, rotation.z)

    local hue = (Luma.Time.GetElapsedSeconds() * self.Properties.ColorSpeed) % 1.0
    local r, g, b = HSVToRGB(hue, 0.75, 1.0)
    Luma.Renderer.SetColor(self.entity, r, g, b, 1.0)
end

return RotateAndTint
