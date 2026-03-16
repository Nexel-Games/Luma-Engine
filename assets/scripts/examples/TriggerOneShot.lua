local TriggerOneShot = {}

TriggerOneShot.Properties = {
    Sound = { type = "asset", default = "Assets/Audio/click.wav", tooltip = "Audio clip to play on trigger enter" },
    Volume = { type = "float", default = 1.0, min = 0.0, max = 2.0 },
    Pitch = { type = "float", default = 1.0, min = 0.25, max = 3.0 }
}

function TriggerOneShot:OnTriggerEnter(other)
    Luma.Audio.PlayOneShot(self.Properties.Sound, {
        volume = self.Properties.Volume,
        pitch = self.Properties.Pitch
    })
end

return TriggerOneShot
